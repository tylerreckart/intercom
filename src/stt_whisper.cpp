#include "intercom/stt_whisper.hpp"
#include "intercom/managed_server.hpp"
#include "intercom/util.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace intercom {
namespace {

std::string run_cmd_capture(const std::string& cmd, int* exit_code) {
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    if (exit_code) *exit_code = -1;
    return {};
  }
  std::string out;
  std::array<char, 4096> buf {};
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
    out += buf.data();
  }
  const int rc = pclose(pipe);
  if (exit_code) {
#if defined(_WIN32)
    *exit_code = rc;
#else
    *exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
  }
  return out;
}

std::string shell_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

std::string parse_whisper_text(const std::string& out) {
  std::string transcript;
  std::istringstream iss(out);
  std::string line;
  while (std::getline(iss, line)) {
    line = trim(line);
    if (line.empty()) continue;
    if (line.rfind("whisper_", 0) == 0) continue;
    if (line.find("system_info:") != std::string::npos) continue;
    if (!transcript.empty()) transcript.push_back(' ');
    transcript += line;
  }
  return trim(transcript);
}

}  // namespace

WhisperStt::WhisperStt(WhisperConfig cfg) : cfg_(std::move(cfg)) {
  cfg_.model = expand_home(cfg_.model);
  cfg_.binary = expand_home(cfg_.binary);
  cfg_.server_binary = expand_home(cfg_.server_binary);
  if (!cfg_.server_url.empty()) {
    server_url_ = cfg_.server_url;
    while (!server_url_.empty() && server_url_.back() == '/') server_url_.pop_back();
  } else if (cfg_.use_server) {
    server_url_ = "http://127.0.0.1:" + std::to_string(cfg_.server_port);
  }
  std::string err;
  if (cfg_.use_server && !server_url_.empty()) {
    if (!ensure_server(&err)) {
      std::cerr << "intercom whisper: warm server unavailable (" << err
                << ") — falling back to CLI" << std::endl;
      child_.reset();
      if (cfg_.server_url.empty()) server_url_.clear();
    }
  }
}

WhisperStt::~WhisperStt() = default;

bool WhisperStt::ensure_server(std::string* err) {
  if (!server_url_.empty() && http_get_ok(server_url_ + "/health", 400)) {
    return true;
  }
  if (!cfg_.server_url.empty()) {
    if (err) *err = "whisper server_url not reachable: " + cfg_.server_url;
    return false;
  }

  std::string server_bin = cfg_.server_binary;
  if (server_bin.empty()) {
    const std::string cli = which_executable(cfg_.binary);
    server_bin = sibling_binary(cli.empty() ? cfg_.binary : cli, "whisper-server");
  }
  if (which_executable(server_bin).empty() && !executable_on_path_or_file(server_bin)) {
    if (err) *err = "whisper-server not found";
    return false;
  }
  if (!file_exists(cfg_.model)) {
    if (err) *err = "whisper model missing: " + cfg_.model;
    return false;
  }

  std::vector<std::string> argv = {
      server_bin,
      "-m",
      cfg_.model,
      "-l",
      cfg_.language,
      "-nt",
      "--host",
      "127.0.0.1",
      "--port",
      std::to_string(cfg_.server_port),
  };
  child_ = std::make_unique<ManagedServer>();
  return child_->start("whisper", argv, server_url_ + "/health", 60000, err);
}

bool WhisperStt::ready(std::string* detail) const {
  if (!server_url_.empty() && http_get_ok(server_url_ + "/health", 800)) {
    if (detail) *detail = "server " + server_url_;
    return true;
  }
  if (!executable_on_path_or_file(cfg_.binary)) {
    if (detail) *detail = "whisper binary not found: " + cfg_.binary;
    return false;
  }
  if (!file_exists(cfg_.model)) {
    if (detail) *detail = "whisper model missing: " + cfg_.model;
    return false;
  }
  if (detail) *detail = "ok (cli)";
  return true;
}

std::string WhisperStt::transcribe(const std::vector<std::uint8_t>& pcm,
                                   int sample_rate,
                                   int channels,
                                   std::string* err) {
  if (pcm.empty()) {
    if (err) *err = "empty pcm";
    return {};
  }
  std::lock_guard<std::mutex> lk(mu_);
  if (!server_url_.empty()) {
    std::string http_err;
    const std::string text = transcribe_http(pcm, sample_rate, channels, &http_err);
    if (!text.empty()) return text;
    if (cfg_.server_url.empty()) {
      std::cerr << "intercom whisper: server transcribe failed (" << http_err
                << ") — trying CLI" << std::endl;
    } else {
      if (err) *err = http_err;
      return {};
    }
  }
  return transcribe_cli(pcm, sample_rate, channels, err);
}

std::string WhisperStt::transcribe_http(const std::vector<std::uint8_t>& pcm,
                                        int sample_rate,
                                        int channels,
                                        std::string* err) {
  auto parsed = parse_http_url(server_url_);
  if (!parsed) {
    if (err) *err = "invalid whisper server_url";
    return {};
  }
  const auto wav = encode_wav_s16le(pcm, sample_rate, channels);
  httplib::MultipartFormDataItems items = {
      {"file", std::string(reinterpret_cast<const char*>(wav.data()), wav.size()),
       "utterance.wav", "audio/wav"},
      {"response_format", "json", "", ""},
      {"language", cfg_.language, "", ""},
      {"temperature", "0.0", "", ""},
  };
  httplib::Client cli(parsed->host, parsed->port);
  cli.set_connection_timeout(5, 0);
  cli.set_read_timeout(cfg_.timeout_seconds > 0 ? cfg_.timeout_seconds : 120, 0);
  cli.set_write_timeout(30, 0);
  const std::string path = parsed->path + "/inference";
  auto res = cli.Post(path.c_str(), items);
  if (!res) {
    if (err) *err = "whisper server: connection failed";
    return {};
  }
  if (res->status != 200) {
    if (err) {
      *err = "whisper server HTTP " + std::to_string(res->status) + ": " + res->body;
    }
    return {};
  }
  try {
    auto j = nlohmann::json::parse(res->body);
    if (j.contains("text") && j["text"].is_string()) {
      const std::string t = trim(j["text"].get<std::string>());
      if (t.empty()) {
        if (err) *err = "whisper returned empty transcript";
        return {};
      }
      return t;
    }
  } catch (const std::exception& e) {
    if (err) *err = std::string("whisper server parse: ") + e.what();
    return {};
  }
  const std::string t = parse_whisper_text(res->body);
  if (t.empty()) {
    if (err) *err = "whisper returned empty transcript";
    return {};
  }
  return t;
}

std::string WhisperStt::transcribe_cli(const std::vector<std::uint8_t>& pcm,
                                       int sample_rate,
                                       int channels,
                                       std::string* err) {
  std::string ready_detail;
  if (!executable_on_path_or_file(cfg_.binary) || !file_exists(cfg_.model)) {
    if (err) *err = ready_detail.empty() ? "whisper not ready" : ready_detail;
    return {};
  }

  const auto tmp = std::filesystem::temp_directory_path() /
                   ("intercom-stt-" + make_turn_id() + ".wav");
  const std::string wav_path = tmp.string();
  if (!write_wav_s16le(wav_path, pcm, sample_rate, channels)) {
    if (err) *err = "failed to write temp wav";
    return {};
  }

  std::ostringstream cmd;
  cmd << shell_quote(cfg_.binary)
      << " -m " << shell_quote(cfg_.model)
      << " -f " << shell_quote(wav_path)
      << " -nt -np -l " << shell_quote(cfg_.language)
      << " 2>/dev/null";

  int exit_code = 0;
  std::string out = run_cmd_capture(cmd.str(), &exit_code);
  std::error_code ec;
  std::filesystem::remove(wav_path, ec);

  if (exit_code != 0 && out.empty()) {
    if (err) *err = "whisper exited with code " + std::to_string(exit_code);
    return {};
  }

  const std::string transcript = parse_whisper_text(out);
  if (transcript.empty()) {
    if (err) *err = "whisper returned empty transcript";
    return {};
  }
  return transcript;
}

}  // namespace intercom
