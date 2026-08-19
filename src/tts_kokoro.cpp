#include "intercom/tts_kokoro.hpp"
#include "intercom/util.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/wait.h>

namespace intercom {
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

bool downmix_to_mono(std::vector<std::uint8_t>* pcm, int channels) {
  if (!pcm || pcm->empty() || channels <= 1) return true;
  if (channels > 1) {
    // Take left channel samples.
    std::vector<std::uint8_t> mono(pcm->size() / static_cast<std::size_t>(channels));
    const auto* in = reinterpret_cast<const std::int16_t*>(pcm->data());
    auto* m = reinterpret_cast<std::int16_t*>(mono.data());
    const std::size_t frames = mono.size() / 2;
    for (std::size_t i = 0; i < frames; ++i) {
      m[i] = in[i * static_cast<std::size_t>(channels)];
    }
    *pcm = std::move(mono);
  }
  return true;
}

}  // namespace

KokoroTts::KokoroTts(KokoroConfig cfg, int target_sample_rate)
    : cfg_(std::move(cfg)), target_sample_rate_(target_sample_rate) {
  cfg_.model = expand_home(cfg_.model);
  cfg_.voices = expand_home(cfg_.voices);
}

bool KokoroTts::ready(std::string* detail) const {
  if (!executable_on_path_or_file(cfg_.binary)) {
    if (detail) *detail = "kokoro-tts binary not found: " + cfg_.binary;
    return false;
  }
  if (!file_exists(cfg_.model)) {
    if (detail) *detail = "kokoro model missing: " + cfg_.model;
    return false;
  }
  if (!file_exists(cfg_.voices)) {
    if (detail) *detail = "kokoro voices missing: " + cfg_.voices;
    return false;
  }
  if (detail) *detail = "ok";
  return true;
}

bool KokoroTts::synthesize(const std::string& text,
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
  const std::string text_path =
      (dir / ("intercom-kokoro-tts-" + id + ".txt")).string();
  const std::string wav_path =
      (dir / ("intercom-kokoro-tts-" + id + ".wav")).string();

  if (!write_text_file(text_path, text)) {
    if (err) *err = "failed to write kokoro text temp file";
    return false;
  }

  std::ostringstream cmd;
  cmd << shell_quote(cfg_.binary)
      << " " << shell_quote(text_path)
      << " " << shell_quote(wav_path)
      << " --voice " << shell_quote(cfg_.voice)
      << " --speed " << cfg_.speed
      << " --format wav"
      << " --model " << shell_quote(cfg_.model)
      << " --voices " << shell_quote(cfg_.voices)
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
    if (err) *err = "kokoro-tts failed (exit " + std::to_string(exit_code) + ")";
    return false;
  }

  int wav_rate = 0;
  int wav_ch = 0;
  auto pcm = read_wav_s16le(wav_path, &wav_rate, &wav_ch);
  std::filesystem::remove(wav_path, ec);
  if (!pcm) {
    if (err) *err = "failed to read kokoro wav";
    return false;
  }

  if (wav_rate <= 0) wav_rate = 24000;
  std::vector<std::uint8_t> out = *pcm;
  downmix_to_mono(&out, wav_ch);

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

}  // namespace intercom

