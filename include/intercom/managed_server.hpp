#pragma once

#include <string>
#include <vector>

#include <sys/types.h>

namespace intercom {

// Owns an optional child process (whisper-server / kokoro_server.py) and
// waits until a health URL returns HTTP 200. Destructor sends SIGTERM.
class ManagedServer {
 public:
  ManagedServer() = default;
  ~ManagedServer();

  ManagedServer(const ManagedServer&) = delete;
  ManagedServer& operator=(const ManagedServer&) = delete;

  // If health_url is already 200, attach without spawning (owned=false).
  bool start(const std::string& name,
             const std::vector<std::string>& argv,
             const std::string& health_url,
             int timeout_ms,
             std::string* err);

  bool running() const;
  bool owned() const { return owned_; }
  pid_t pid() const { return pid_; }
  const std::string& log_path() const { return log_path_; }

  void stop();

 private:
  pid_t pid_ = -1;
  bool owned_ = false;
  std::string name_;
  std::string log_path_;
};

bool http_get_ok(const std::string& url, int timeout_ms, std::string* body = nullptr);

std::string which_executable(const std::string& binary);
std::string sibling_binary(const std::string& resolved_path, const std::string& name);
std::string shebang_interpreter(const std::string& script_path);
std::string this_executable_dir();

}  // namespace intercom
