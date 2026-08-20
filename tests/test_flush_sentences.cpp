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

void expect_eq(const std::string& a, const std::string& b, const char* file, int line) {
  if (a != b) {
    std::cerr << "FAIL " << file << ":" << line << " \"" << a << "\" != \"" << b << "\"\n";
    ++g_fails;
  }
}

void expect_size(const std::vector<std::string>& v, std::size_t n, const char* file, int line) {
  if (v.size() != n) {
    std::cerr << "FAIL " << file << ":" << line << " size " << v.size() << " != " << n << "\n";
    ++g_fails;
  }
}

#define CHECK(cond) expect((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) expect_eq((a), (b), __FILE__, __LINE__)
#define CHECK_SIZE(v, n) expect_size((v), (n), __FILE__, __LINE__)

void test_incremental_then_final() {
  std::string buf = "Hello. How";
  auto first = intercom::flush_sentences(buf, false);
  CHECK_SIZE(first, 1);
  CHECK_EQ(first[0], "Hello.");
  CHECK_EQ(buf, "How");

  buf += " are you?";
  auto second = intercom::flush_sentences(buf, true);
  CHECK_SIZE(second, 1);
  CHECK_EQ(second[0], "How are you?");
  CHECK(buf.empty());
}

void test_no_emit_until_boundary() {
  std::string buf = "Hello there";
  auto none = intercom::flush_sentences(buf, false);
  CHECK(none.empty());
  CHECK_EQ(buf, "Hello there");
}

void test_final_flush_unpunctuated() {
  std::string buf = "no period yet";
  auto rest = intercom::flush_sentences(buf, true);
  CHECK_SIZE(rest, 1);
  CHECK_EQ(rest[0], "no period yet");
  CHECK(buf.empty());
}

void test_multiple_sentences() {
  std::string buf = "One. Two! Three? leftover";
  auto s = intercom::flush_sentences(buf, false);
  CHECK_SIZE(s, 3);
  CHECK_EQ(s[0], "One.");
  CHECK_EQ(s[1], "Two!");
  CHECK_EQ(s[2], "Three?");
  CHECK_EQ(buf, "leftover");
}

}  // namespace

int main() {
  test_incremental_then_final();
  test_no_emit_until_boundary();
  test_final_flush_unpunctuated();
  test_multiple_sentences();
  if (g_fails != 0) {
    std::cerr << g_fails << " failure(s)\n";
    return 1;
  }
  std::cout << "test_flush_sentences ok\n";
  return 0;
}
