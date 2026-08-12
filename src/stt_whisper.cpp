#include "alfred/stt_whisper.hpp"
#include "alfred/util.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <sys/wait.h>

namespace alfred {
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

}  // namespace

WhisperStt::WhisperStt(WhisperConfig cfg) : cfg_(std::move(cfg)) {
  cfg_.model = expand_home(cfg_.model);
}

bool WhisperStt::ready(std::string* detail) const {
  if (!executable_on_path_or_file(cfg_.binary)) {
    if (detail) *detail = "whisper binary not found: " + cfg_.binary;
    return false;
  }
  if (!file_exists(cfg_.model)) {
    if (detail) *detail = "whisper model missing: " + cfg_.model;
    return false;
  }
  if (detail) *detail = "ok";
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
  std::string ready_detail;
  if (!ready(&ready_detail)) {
    if (err) *err = ready_detail;
    return {};
  }

  const auto tmp = std::filesystem::temp_directory_path() /
                   ("alfred-stt-" + make_turn_id() + ".wav");
  const std::string wav_path = tmp.string();
  if (!write_wav_s16le(wav_path, pcm, sample_rate, channels)) {
    if (err) *err = "failed to write temp wav";
    return {};
  }

  // Prefer whisper-cli flags; also try main-style -m/-f for older builds.
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

  // whisper-cli prints transcript lines; take non-empty trimmed lines joined.
  std::string transcript;
  std::istringstream iss(out);
  std::string line;
  while (std::getline(iss, line)) {
    line = trim(line);
    if (line.empty()) continue;
    // Skip common progress noise
    if (line.rfind("whisper_", 0) == 0) continue;
    if (line.find("system_info:") != std::string::npos) continue;
    if (!transcript.empty()) transcript.push_back(' ');
    transcript += line;
  }
  transcript = trim(transcript);
  if (transcript.empty()) {
    if (err) *err = "whisper returned empty transcript";
    return {};
  }
  return transcript;
}

}  // namespace alfred
