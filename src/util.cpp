#include "alfred/util.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace alfred {
namespace {

#pragma pack(push, 1)
struct WavHeader {
  char riff[4] = {'R', 'I', 'F', 'F'};
  std::uint32_t chunk_size = 0;
  char wave[4] = {'W', 'A', 'V', 'E'};
  char fmt[4] = {'f', 'm', 't', ' '};
  std::uint32_t fmt_size = 16;
  std::uint16_t audio_format = 1;
  std::uint16_t num_channels = 1;
  std::uint32_t sample_rate = 16000;
  std::uint32_t byte_rate = 0;
  std::uint16_t block_align = 0;
  std::uint16_t bits_per_sample = 16;
  char data[4] = {'d', 'a', 't', 'a'};
  std::uint32_t data_size = 0;
};
#pragma pack(pop)

}  // namespace

std::string expand_home(std::string path) {
  if (path.empty() || path[0] != '~') return path;
  const char* home = std::getenv("HOME");
  if (!home) return path;
  if (path.size() == 1) return std::string(home);
  if (path[1] == '/') return std::string(home) + path.substr(1);
  return path;
}

std::string make_turn_id() {
  static thread_local std::mt19937_64 rng{
      static_cast<std::uint64_t>(
          std::chrono::high_resolution_clock::now().time_since_epoch().count()) ^
      static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(&rng))};
  std::uniform_int_distribution<std::uint64_t> dist;
  std::ostringstream oss;
  oss << std::hex << dist(rng) << dist(rng);
  return oss.str();
}

bool file_exists(const std::string& path) {
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool executable_on_path_or_file(const std::string& binary) {
  if (binary.find('/') != std::string::npos) {
    return ::access(binary.c_str(), X_OK) == 0;
  }
  const char* path_env = std::getenv("PATH");
  if (!path_env) return false;
  std::string path = path_env;
  std::size_t start = 0;
  while (start <= path.size()) {
    std::size_t end = path.find(':', start);
    if (end == std::string::npos) end = path.size();
    std::string dir = path.substr(start, end - start);
    if (!dir.empty()) {
      std::string full = dir + "/" + binary;
      if (::access(full.c_str(), X_OK) == 0) return true;
    }
    if (end == path.size()) break;
    start = end + 1;
  }
  return false;
}

bool write_wav_s16le(const std::string& path,
                     const std::vector<std::uint8_t>& pcm,
                     int sample_rate,
                     int channels) {
  WavHeader h;
  h.num_channels = static_cast<std::uint16_t>(channels);
  h.sample_rate = static_cast<std::uint32_t>(sample_rate);
  h.bits_per_sample = 16;
  h.block_align = static_cast<std::uint16_t>(channels * 2);
  h.byte_rate = static_cast<std::uint32_t>(sample_rate * h.block_align);
  h.data_size = static_cast<std::uint32_t>(pcm.size());
  h.chunk_size = 36 + h.data_size;

  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(&h), sizeof(h));
  out.write(reinterpret_cast<const char*>(pcm.data()),
            static_cast<std::streamsize>(pcm.size()));
  return static_cast<bool>(out);
}

std::optional<std::vector<std::uint8_t>> read_wav_s16le(const std::string& path,
                                                        int* out_sample_rate,
                                                        int* out_channels) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;

  char riff[12];
  in.read(riff, 12);
  if (in.gcount() != 12 || std::strncmp(riff, "RIFF", 4) != 0 ||
      std::strncmp(riff + 8, "WAVE", 4) != 0) {
    return std::nullopt;
  }

  std::uint16_t audio_format = 0;
  std::uint16_t channels = 0;
  std::uint32_t sample_rate = 0;
  std::uint16_t bits = 0;
  std::vector<std::uint8_t> pcm;

  while (in) {
    char id[4];
    std::uint32_t size = 0;
    in.read(id, 4);
    in.read(reinterpret_cast<char*>(&size), 4);
    if (!in) break;
    if (std::strncmp(id, "fmt ", 4) == 0) {
      std::vector<char> buf(size);
      in.read(buf.data(), size);
      if (size >= 16) {
        std::memcpy(&audio_format, buf.data(), 2);
        std::memcpy(&channels, buf.data() + 2, 2);
        std::memcpy(&sample_rate, buf.data() + 4, 4);
        std::memcpy(&bits, buf.data() + 14, 2);
      }
      if (size % 2) in.ignore(1);
    } else if (std::strncmp(id, "data", 4) == 0) {
      pcm.resize(size);
      in.read(reinterpret_cast<char*>(pcm.data()), size);
      if (size % 2) in.ignore(1);
    } else {
      in.ignore(size + (size % 2));
    }
  }

  if (audio_format != 1 || bits != 16 || channels < 1 || pcm.empty()) {
    return std::nullopt;
  }
  if (out_sample_rate) *out_sample_rate = static_cast<int>(sample_rate);
  if (out_channels) *out_channels = static_cast<int>(channels);
  return pcm;
}

std::vector<std::uint8_t> resample_s16le_mono(const std::vector<std::uint8_t>& pcm,
                                              int from_rate,
                                              int to_rate) {
  if (from_rate <= 0 || to_rate <= 0 || pcm.size() < 2) return pcm;
  if (from_rate == to_rate) return pcm;

  const std::size_t in_samples = pcm.size() / 2;
  const auto* in = reinterpret_cast<const std::int16_t*>(pcm.data());
  const double ratio = static_cast<double>(from_rate) / static_cast<double>(to_rate);
  const std::size_t out_samples =
      static_cast<std::size_t>(std::floor(static_cast<double>(in_samples) / ratio));
  std::vector<std::uint8_t> out(out_samples * 2);
  auto* o = reinterpret_cast<std::int16_t*>(out.data());
  auto at = [&](std::ptrdiff_t i) -> double {
    if (i < 0) i = 0;
    if (static_cast<std::size_t>(i) >= in_samples) i = static_cast<std::ptrdiff_t>(in_samples - 1);
    return static_cast<double>(in[i]);
  };
  for (std::size_t i = 0; i < out_samples; ++i) {
    const double src = static_cast<double>(i) * ratio;
    const auto i1 = static_cast<std::ptrdiff_t>(src);
    const double t = src - static_cast<double>(i1);
    const double y0 = at(i1 - 1);
    const double y1 = at(i1);
    const double y2 = at(i1 + 1);
    const double y3 = at(i1 + 2);
    const double c0 = y1;
    const double c1 = 0.5 * (y2 - y0);
    const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
    const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    const double s = ((c3 * t + c2) * t + c1) * t + c0;
    const double clipped = std::max(-32768.0, std::min(32767.0, s));
    o[i] = static_cast<std::int16_t>(std::lround(clipped));
  }
  return out;
}

std::string to_lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return std::string(s);
}

std::vector<std::string> flush_sentences(std::string& buf, bool final_flush) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i < buf.size(); ++i) {
    const char c = buf[i];
    if (c == '.' || c == '!' || c == '?') {
      const bool boundary =
          (i + 1 == buf.size()) ||
          std::isspace(static_cast<unsigned char>(buf[i + 1])) ||
          buf[i + 1] == '"' || buf[i + 1] == '\'';
      if (boundary) {
        std::string sentence = trim(buf.substr(start, i - start + 1));
        if (!sentence.empty()) out.push_back(std::move(sentence));
        start = i + 1;
        while (start < buf.size() &&
               std::isspace(static_cast<unsigned char>(buf[start]))) {
          ++start;
        }
      }
    }
  }
  if (final_flush) {
    std::string rest = trim(buf.substr(start));
    if (!rest.empty()) out.push_back(std::move(rest));
    buf.clear();
  } else {
    buf.erase(0, start);
  }
  return out;
}

}  // namespace alfred
