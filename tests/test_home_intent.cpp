#include "intercom/home.hpp"

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

}  // namespace

int main() {
  auto timer = intercom::parse_home_intent("set a timer for 5 minutes");
  CHECK(timer.has_value());
  if (timer) {
    CHECK(timer->kind == intercom::HomeIntentKind::Timer);
    CHECK(timer->timer_seconds == 300);
  }

  auto half = intercom::parse_home_intent("set a timer for half an hour");
  CHECK(half.has_value());
  if (half) CHECK(half->timer_seconds == 1800);

  auto twenty_five = intercom::parse_home_intent("remind me in twenty five minutes");
  CHECK(twenty_five.has_value());
  if (twenty_five) {
    CHECK(twenty_five->kind == intercom::HomeIntentKind::Timer);
    CHECK(twenty_five->timer_seconds == 1500);
  }

  auto bare = intercom::parse_home_intent("set a timer");
  CHECK(bare.has_value());
  if (bare) {
    CHECK(bare->kind == intercom::HomeIntentKind::Timer);
    CHECK(bare->timer_seconds == 0);
  }

  auto kitchen = intercom::parse_home_intent("turn on the kitchen lights");
  CHECK(kitchen.has_value());
  if (kitchen) {
    CHECK(kitchen->kind == intercom::HomeIntentKind::LightOn);
    CHECK(kitchen->room == "kitchen");
  }

  auto off = intercom::parse_home_intent("turn off the lights");
  CHECK(off.has_value());
  if (off) CHECK(off->kind == intercom::HomeIntentKind::LightOff);

  auto toggle = intercom::parse_home_intent("toggle the bedroom lights");
  CHECK(toggle.has_value());
  if (toggle) {
    CHECK(toggle->kind == intercom::HomeIntentKind::LightToggle);
    CHECK(toggle->room == "bedroom");
  }

  auto living = intercom::parse_home_intent("switch off the living room lights");
  CHECK(living.has_value());
  if (living) {
    CHECK(living->kind == intercom::HomeIntentKind::LightOff);
    CHECK(living->room == "living room");
  }

  auto weather = intercom::parse_home_intent("what's the weather");
  CHECK(weather.has_value());
  if (weather) CHECK(weather->kind == intercom::HomeIntentKind::Weather);

  CHECK(!intercom::parse_home_intent("what's the weather in Tokyo").has_value());
  CHECK(!intercom::parse_home_intent("hello").has_value());
  CHECK(!intercom::parse_home_intent("what time is it").has_value());

  auto up = intercom::parse_home_intent("turn the volume up");
  CHECK(up.has_value());
  if (up) CHECK(up->kind == intercom::HomeIntentKind::VolumeUp);

  auto down = intercom::parse_home_intent("volume down");
  CHECK(down.has_value());
  if (down) CHECK(down->kind == intercom::HomeIntentKind::VolumeDown);

  auto quieter = intercom::parse_home_intent("quieter");
  CHECK(quieter.has_value());
  if (quieter) CHECK(quieter->kind == intercom::HomeIntentKind::VolumeDown);

  auto alarm = intercom::parse_home_intent("what's my next alarm");
  CHECK(alarm.has_value());
  if (alarm) CHECK(alarm->kind == intercom::HomeIntentKind::NextAlarm);

  CHECK(intercom::spoken_duration(300) == "five minutes");
  CHECK(intercom::spoken_duration(60) == "one minute");
  CHECK(intercom::spoken_duration(3600) == "one hour");
  CHECK(intercom::spoken_number(12) == "twelve");
  CHECK(intercom::spoken_number(21) == "twenty one");

  CHECK(std::string(intercom::home_intent_kind_name(intercom::HomeIntentKind::Timer)) ==
        "timer");

  if (g_fails != 0) {
    std::cerr << g_fails << " failure(s)\n";
    return 1;
  }
  std::cout << "test_home_intent ok\n";
  return 0;
}
