#include "intercom/util.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_fails = 0;

void expect(bool cond, const char* expr, const char* file, int line) {
  if (!cond) {
    std::cerr << "FAIL " << file << ":" << line << " " << expr << "\n";
    ++g_fails;
  }
}

#define CHECK(cond) expect((cond), #cond, __FILE__, __LINE__)

}  // namespace

int main() {
  const std::vector<std::uint8_t> pcm = {0x00, 0x10, 0xff, 0x7f, 0x00, 0x00};
  const auto wav = intercom::encode_wav_s16le(pcm, 24000, 1);
  int rate = 0;
  int ch = 0;
  auto decoded = intercom::parse_wav_s16le(wav.data(), wav.size(), &rate, &ch);
  CHECK(decoded.has_value());
  CHECK(rate == 24000);
  CHECK(ch == 1);
  if (decoded) CHECK(*decoded == pcm);

  auto url = intercom::parse_http_url("http://127.0.0.1:8091/health");
  CHECK(url.has_value());
  if (url) {
    CHECK(url->host == "127.0.0.1");
    CHECK(url->port == 8091);
    CHECK(url->path == "/health");
    CHECK(!url->https);
  }

  auto bare = intercom::parse_http_url("http://127.0.0.1:8092");
  CHECK(bare.has_value());
  if (bare) {
    CHECK(bare->path.empty());
    CHECK(bare->port == 8092);
  }

  std::vector<std::uint8_t> fade_pcm(8, 0);
  auto* samples = reinterpret_cast<std::int16_t*>(fade_pcm.data());
  samples[0] = 30000;
  samples[1] = 30000;
  samples[2] = 30000;
  samples[3] = 30000;
  intercom::fade_s16le_mono_edges(&fade_pcm, 1000, 2, 2);
  CHECK(samples[0] > 0 && samples[0] < 30000);
  CHECK(samples[3] > 0 && samples[3] < 30000);

  const auto quiet = intercom::silence_s16le_mono(1000, 10);
  CHECK(quiet.size() == 20);
  CHECK(quiet[0] == 0);

  if (g_fails != 0) {
    std::cerr << g_fails << " failure(s)\n";
    return 1;
  }
  std::cout << "test_wav_util ok\n";
  return 0;
}
