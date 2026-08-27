#include "intercom/fast_path.hpp"
#include "intercom/clock.hpp"

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

#define CHECK(cond) expect((cond), #cond, __FILE__, __LINE__)

bool contains(const std::string& s, const char* needle) {
  return s.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  intercom::FastPath off(false);
  CHECK(!off.try_handle("hello").has_value());

  intercom::FastPath on(true);
  CHECK(!on.try_handle("what's the weather").has_value());
  CHECK(!on.try_handle("good morning, what's the weather").has_value());

  auto morning = on.try_handle("good morning");
  CHECK(morning.has_value());
  if (morning) {
    CHECK(contains(morning->reply, "sir"));
    CHECK(contains(morning->reply, "orning"));
    CHECK(morning->reply.find('?') != std::string::npos);
    CHECK(!contains(morning->reply, "Arthur here"));
  }

  auto punct = on.try_handle("Good morning, Arthur.");
  CHECK(punct.has_value());
  if (punct) {
    CHECK(contains(punct->reply, "sir"));
    CHECK(punct->reply.find('?') != std::string::npos);
  }

  auto hi = on.try_handle("hey");
  CHECK(hi.has_value());
  if (hi) {
    CHECK(contains(hi->reply, "sir"));
    CHECK(hi->reply.find('?') != std::string::npos);
    CHECK(!contains(hi->reply, "Arthur here"));
  }

  auto how = on.try_handle("how are you");
  CHECK(how.has_value());
  if (how) {
    CHECK(contains(how->reply, "sir"));
    CHECK(how->reply.find('?') != std::string::npos);
  }

  auto night = on.try_handle("good night");
  CHECK(night.has_value());
  if (night) {
    CHECK(contains(night->reply, "sir"));
    CHECK(night->reply.find('?') == std::string::npos);
  }

  auto status = on.try_handle("are you there");
  CHECK(status.has_value());
  if (status) {
    CHECK(contains(status->reply, "sir"));
    CHECK(!contains(status->reply, "Speech bridge"));
  }

  auto echo = on.try_handle("echo hello there");
  CHECK(echo.has_value());
  if (echo) CHECK(echo->reply == "hello there");

  auto clock = on.try_handle("what time is it");
  CHECK(clock.has_value());
  if (clock) {
    CHECK(contains(clock->reply, "It's "));
    CHECK(contains(clock->reply, "sir"));
    CHECK(!contains(clock->reply, "AM"));
    CHECK(!contains(clock->reply, "PM"));
  }

  auto date = on.try_handle("what's the date");
  CHECK(date.has_value());
  if (date) {
    CHECK(contains(date->reply, "It's "));
    CHECK(contains(date->reply, "sir"));
  }

  CHECK(intercom::is_social_turn("good morning"));
  CHECK(intercom::is_social_turn("Good morning, Arthur."));
  CHECK(intercom::withholds_fillers("good morning"));
  CHECK(intercom::withholds_fillers("what time is it"));
  CHECK(!intercom::withholds_fillers("what's the weather"));
  CHECK(!intercom::is_clock_query("what time is it in Tokyo"));

  const std::string rule = intercom::local_clock_rule();
  CHECK(contains(rule, "CURRENT LOCAL DATETIME:"));
  CHECK(contains(rule, "never look it up"));

  if (g_fails != 0) {
    std::cerr << g_fails << " failure(s)\n";
    return 1;
  }
  std::cout << "test_fast_path ok\n";
  return 0;
}
