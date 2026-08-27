#include "intercom/speakable.hpp"

#include <iostream>
#include <string>

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

#define CHECK(cond) expect((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) expect_eq((a), (b), __FILE__, __LINE__)

bool contains(const std::string& s, const char* needle) {
  return s.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  CHECK_EQ(intercom::to_speakable("well-known"), "well known.");
  CHECK(!contains(intercom::to_speakable("well-known"), "minus"));
  CHECK(!contains(intercom::to_speakable("state-of-the-art"), "minus"));
  CHECK_EQ(intercom::to_speakable("It's the 14th."), "It's the fourteenth.");
  CHECK_EQ(intercom::to_speakable("the 1st, 2nd, 3rd and 21st"),
           "the first, second, third and twenty first.");
  CHECK_EQ(intercom::to_speakable("14th"), "fourteenth.");
  CHECK_EQ(intercom::to_speakable("14 th"), "fourteenth.");
  CHECK(!contains(intercom::to_speakable("On the 14th of August"), "14"));
  CHECK(contains(intercom::to_speakable("On the 14th of August"), "fourteenth"));
  CHECK_EQ(intercom::to_speakable("rooms 14-16"), "rooms 14 to 16.");
  CHECK(contains(intercom::to_speakable("2 - 3"), "minus"));
  CHECK(contains(intercom::to_speakable("-5"), "negative"));
  CHECK(contains(intercom::to_speakable("Hello — world"), ","));
  CHECK(!contains(intercom::to_speakable("Hello — world"), "minus"));
  CHECK_EQ(intercom::to_speakable("Wait... okay"), "Wait, okay.");
  CHECK(contains(intercom::to_speakable("2 * 3"), "times"));

  if (g_fails != 0) {
    std::cerr << g_fails << " failure(s)\n";
    return 1;
  }
  std::cout << "test_speakable ok\n";
  return 0;
}
