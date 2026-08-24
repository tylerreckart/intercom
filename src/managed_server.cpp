#include "intercom/managed_server.hpp"
#include "intercom/util.hpp"

#include <cstdint>
#include <httplib.h>

#include <cerrno>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <limits.h>

namespace intercom {
namespace {

bool child_exited(pid_t pid, int* status_out) {
  if (pid <= 0) return true;
  int status = 0;
  const pid_t r = ::waitpid(pid, &status, WNOHANG);
  if (r == 0) return false;
  if (status_out) *status_out = status;
  return r == pid || r < 0;
}

}  // namespace

std::string which_executable(const std::string& binary) {
  if (binary.empty()) return {};
  if (binary.find('/') != std::string::npos) {
    return ::access(binary.c_str(), X_OK) == 0 ? binary : std::string{};
  }
  const char* path_env = std::getenv("PATH");
  if (!path_env) return {};
  const std::string path = path_env;
  std::size_t start = 0;
  while (start <= path.size()) {
    std::size_t end = path.find(':', start);
    if (end == std::string::npos) end = path.size();
    const std::string dir = path.substr(start, end - start);
    if (!dir.empty()) {
      const std::string full = dir + "/" + binary;
      if (::access(full.c_str(), X_OK) == 0) return full;
    }
    if (end == path.size()) break;
    start = end + 1;
  }
  return {};
}

std::string sibling_binary(const std::string& resolved_path, const std::string& name) {
  if (resolved_path.empty() || name.empty()) return {};
  std::string dir = resolved_path;
  const auto slash = dir.rfind('/');
  if (slash == std::string::npos) return which_executable(name);
  dir.resize(slash);
  const std::string full = dir + "/" + name;
  if (::access(full.c_str(), X_OK) == 0) return full;
  return which_executable(name);
}

std::string shebang_interpreter(const std::string& script_path) {
  std::ifstream in(script_path);
  if (!in) return {};
  std::string line;
  if (!std::getline(in, line) || line.size() < 3 || line[0] != '#' || line[1] != '!') {
    return {};
  }
  std::string rest = trim(line.substr(2));
  constexpr std::string_view env = "/usr/bin/env ";
  if (rest.rfind(env.data(), 0) == 0) {
    rest = trim(rest.substr(env.size()));
  }
  const auto sp = rest.find(' ');
  if (sp != std::string::npos) rest.resize(sp);
  return rest;
}

std::string this_executable_dir() {
#ifdef __APPLE__
  char buf[PATH_MAX];
  std::uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) != 0) return {};
  const char* resolved = ::realpath(buf, nullptr);
  std::string path = resolved ? resolved : buf;
  if (resolved) std::free(const_cast<char*>(resolved));
#else
  char buf[PATH_MAX];
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return {};
  buf[n] = '\0';
  std::string path = buf;
#endif
  const auto slash = path.rfind('/');
  if (slash == std::string::npos) return {};
  return path.substr(0, slash);
}

bool http_get_ok(const std::string& url, int timeout_ms, std::string* body) {
  auto parsed = parse_http_url(url);
  if (!parsed) return false;
  httplib::Client cli(parsed->host, parsed->port);
  const int sec = std::max(1, timeout_ms / 1000);
  const int usec = (timeout_ms % 1000) * 1000;
  cli.set_connection_timeout(sec, usec);
  cli.set_read_timeout(sec, usec);
  const std::string path = parsed->path.empty() ? "/" : parsed->path;
  auto res = cli.Get(path.c_str());
  if (!res) return false;
  if (body) *body = res->body;
  return res->status == 200;
}

ManagedServer::~ManagedServer() { stop(); }

bool ManagedServer::start(const std::string& name,
                          const std::vector<std::string>& argv,
                          const std::string& health_url,
                          int timeout_ms,
                          std::string* err) {
  stop();
  name_ = name;
  log_path_ = "/tmp/intercom-" + name + ".log";

  if (!health_url.empty() && http_get_ok(health_url, 400)) {
    owned_ = false;
    pid_ = -1;
    std::cout << "intercom " << name << ": using existing " << health_url << std::endl;
    return true;
  }

  if (argv.empty()) {
    if (err) *err = name + ": empty argv";
    return false;
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    if (err) *err = name + ": fork failed";
    return false;
  }
  if (pid == 0) {
    ::setpgid(0, 0);
    const int fd = ::open(log_path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
      ::dup2(fd, STDOUT_FILENO);
      ::dup2(fd, STDERR_FILENO);
      if (fd > 2) ::close(fd);
    }
    std::vector<std::string> args = argv;
    std::vector<char*> cargs;
    cargs.reserve(args.size() + 1);
    for (auto& s : args) cargs.push_back(s.data());
    cargs.push_back(nullptr);
    ::execvp(cargs[0], cargs.data());
    std::fprintf(stderr, "execvp failed: %s (%s)\n", cargs[0], std::strerror(errno));
    _exit(127);
  }

  ::setpgid(pid, pid);
  pid_ = pid;
  owned_ = true;

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    if (child_exited(pid_, &status)) {
      pid_ = -1;
      owned_ = false;
      if (err) {
        *err = name + " exited before ready (see " + log_path_ + ")";
      }
      return false;
    }
    if (!health_url.empty() && http_get_ok(health_url, 400)) {
      std::cout << "intercom " << name << ": ready at " << health_url << " (pid "
                << pid_ << ", log " << log_path_ << ")" << std::endl;
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (err) *err = name + " health timed out (" + health_url + ", see " + log_path_ + ")";
  stop();
  return false;
}

bool ManagedServer::running() const {
  if (!owned_) {
    return true;
  }
  if (pid_ <= 0) return false;
  return !child_exited(pid_, nullptr);
}

void ManagedServer::stop() {
  if (!owned_ || pid_ <= 0) {
    pid_ = -1;
    owned_ = false;
    return;
  }
  ::kill(-pid_, SIGTERM);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    if (child_exited(pid_, nullptr)) {
      pid_ = -1;
      owned_ = false;
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ::kill(-pid_, SIGKILL);
  ::waitpid(pid_, nullptr, 0);
  pid_ = -1;
  owned_ = false;
}

}  // namespace intercom
