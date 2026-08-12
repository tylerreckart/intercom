#include "alfred/tts_piper.hpp"
#include "alfred/util.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/wait.h>

namespace alfred {
namespace {

std::string shell_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

bool write_text_file(const std::string& path, const std::string& text) {
  std::ofstream out(path);
  if (!out) return false;
  out << text;
  return static_cast<bool>(out);
}

}  // namespace

PiperTts::PiperTts(PiperConfig cfg, int target_sample_rate)
    : cfg_(std::move(cfg)), target_sample_rate_(target_sample_rate) {
  cfg_.model = expand_home(cfg_.model);
}

bool PiperTts::ready(std::string* detail) const {
  if (!executable_on_path_or_file(cfg_.binary)) {
    if (detail) *detail = "piper binary not found: " + cfg_.binary;
    return false;
  }
  if (!file_exists(cfg_.model)) {
    if (detail) *detail = "piper model missing: " + cfg_.model;
    return false;
  }
  if (detail) *detail = "ok";
  return true;
}

bool PiperTts::synthesize(const std::string& text,
                          PcmChunkFn on_chunk,
                          std::string* err) {
  if (text.empty()) return true;
  std::string ready_detail;
  if (!ready(&ready_detail)) {
    if (err) *err = ready_detail;
    return false;
  }

  const std::string id = make_turn_id();
  const auto dir = std::filesystem::temp_directory_path();
  const std::string text_path = (dir / ("alfred-tts-" + id + ".txt")).string();
  const std::string wav_path = (dir / ("alfred-tts-" + id + ".wav")).string();

  if (!write_text_file(text_path, text)) {
    if (err) *err = "failed to write piper text temp file";
    return false;
  }

  std::ostringstream cmd;
  cmd << shell_quote(cfg_.binary)
      << " --model " << shell_quote(cfg_.model)
      << " --output_file " << shell_quote(wav_path)
      << " < " << shell_quote(text_path)
      << " 2>/dev/null";

  const int rc = std::system(cmd.str().c_str());
  std::error_code ec;
  std::filesystem::remove(text_path, ec);
#if !defined(_WIN32)
  const int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#else
  const int exit_code = rc;
#endif
  if (exit_code != 0 || !file_exists(wav_path)) {
    std::filesystem::remove(wav_path, ec);
    if (err) *err = "piper failed (exit " + std::to_string(exit_code) + ")";
    return false;
  }

  int wav_rate = 0;
  int wav_ch = 0;
  auto pcm = read_wav_s16le(wav_path, &wav_rate, &wav_ch);
  std::filesystem::remove(wav_path, ec);
  if (!pcm) {
    if (err) *err = "failed to read piper wav";
    return false;
  }

  std::vector<std::uint8_t> out = *pcm;
  if (wav_ch != 1) {
    // Downmix: take left channel only for multi-channel.
    if (wav_ch > 1) {
      std::vector<std::uint8_t> mono(out.size() / static_cast<std::size_t>(wav_ch));
      const auto* in = reinterpret_cast<const std::int16_t*>(out.data());
      auto* m = reinterpret_cast<std::int16_t*>(mono.data());
      const std::size_t frames = mono.size() / 2;
      for (std::size_t i = 0; i < frames; ++i) {
        m[i] = in[i * static_cast<std::size_t>(wav_ch)];
      }
      out = std::move(mono);
    }
  }
  if (wav_rate <= 0) wav_rate = cfg_.native_sample_rate;
  if (wav_rate != target_sample_rate_) {
    out = resample_s16le_mono(out, wav_rate, target_sample_rate_);
  }

  constexpr std::size_t kChunk = 4096;
  for (std::size_t off = 0; off < out.size(); off += kChunk) {
    const std::size_t n = std::min(kChunk, out.size() - off);
    if (on_chunk && !on_chunk(out.data() + off, n)) {
      if (err) *err = "tts sink aborted";
      return false;
    }
  }
  return true;
}

}  // namespace alfred
