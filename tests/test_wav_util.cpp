#include "intercom/util.hpp"

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

  if (g_fails != 0) {
    std::cerr << g_fails << " failure(s)\n";
    return 1;
  }
  std::cout << "test_wav_util ok\n";
  return 0;
}
