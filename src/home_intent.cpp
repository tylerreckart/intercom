#include "intercom/home.hpp"
#include "intercom/util.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace intercom {
namespace {

bool is_word_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

bool has_word(std::string_view t, std::string_view word) {
  std::size_t pos = 0;
  while (pos <= t.size()) {
    const auto found = t.find(word, pos);
    if (found == std::string_view::npos) return false;
    const bool left = found == 0 || !is_word_char(t[found - 1]);
    const bool right =
        found + word.size() == t.size() || !is_word_char(t[found + word.size()]);
    if (left && right) return true;
    pos = found + 1;
  }
  return false;
}

bool has_any(std::string_view t, std::initializer_list<const char*> words) {
  for (const char* w : words) {
    if (has_word(t, w)) return true;
  }
  return false;
}

std::vector<std::string> tokens(std::string_view t) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : t) {
    if (c == ' ') {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

int word_number(std::string_view w) {
  if (w == "a" || w == "an" || w == "one") return 1;
  if (w == "two") return 2;
  if (w == "three") return 3;
  if (w == "four") return 4;
  if (w == "five") return 5;
  if (w == "six") return 6;
  if (w == "seven") return 7;
  if (w == "eight") return 8;
  if (w == "nine") return 9;
  if (w == "ten") return 10;
  if (w == "eleven") return 11;
  if (w == "twelve") return 12;
  if (w == "thirteen") return 13;
  if (w == "fourteen") return 14;
  if (w == "fifteen") return 15;
  if (w == "sixteen") return 16;
  if (w == "seventeen") return 17;
  if (w == "eighteen") return 18;
  if (w == "nineteen") return 19;
  if (w == "twenty") return 20;
  if (w == "thirty") return 30;
  if (w == "forty") return 40;
  if (w == "fifty") return 50;
  if (w == "sixty") return 60;
  if (w == "ninety") return 90;
  try {
    std::size_t idx = 0;
    const int n = std::stoi(std::string(w), &idx);
    if (idx == w.size() && n > 0) return n;
  } catch (...) {
  }
  return 0;
}

int unit_seconds(std::string_view w) {
  if (w == "second" || w == "seconds") return 1;
  if (w == "minute" || w == "minutes") return 60;
  if (w == "hour" || w == "hours") return 3600;
  return 0;
}

int parse_duration_seconds(std::string_view t) {
  if (has_word(t, "half") && has_any(t, {"hour", "hours"})) return 1800;
  if (has_word(t, "quarter") && has_any(t, {"hour", "hours"})) return 900;

  const auto tok = tokens(t);
  int best = 0;
  for (std::size_t i = 0; i < tok.size(); ++i) {
    int n = word_number(tok[i]);
    if (n == 0) continue;
    if (i + 1 < tok.size()) {
      const int n2 = word_number(tok[i + 1]);
      if (n2 > 0 && n2 < 10 && n >= 20 && n % 10 == 0) {
        n += n2;
        if (i + 2 < tok.size()) {
          if (const int u = unit_seconds(tok[i + 2])) {
            best = std::max(best, n * u);
            continue;
          }
        }
      }
    }
    if (i + 1 < tok.size()) {
      if (const int u = unit_seconds(tok[i + 1])) {
        best = std::max(best, n * u);
      }
    }
  }
  return best;
}

bool looks_like_timer(std::string_view t) {
  if (has_any(t, {"timer", "countdown"})) return true;
  if (has_word(t, "remind") || has_word(t, "reminder")) return true;
  return false;
}

bool looks_like_weather(std::string_view t) {
  if (has_any(t, {"weather", "forecast", "temperature"})) return true;
  if (t == "is it raining" || t == "is it going to rain") return true;
  if (has_word(t, "raining") && (has_word(t, "is") || has_word(t, "it"))) return true;
  return false;
}

bool looks_like_alarm(std::string_view t) {
  return has_word(t, "alarm") &&
         has_any(t, {"next", "when", "what", "time", "my"});
}

bool looks_like_volume(std::string_view t) {
  if (has_word(t, "volume")) return true;
  if (has_any(t, {"louder", "quieter", "softer"})) return true;
  if ((has_word(t, "turn") || has_word(t, "put")) &&
      has_any(t, {"up", "down"}) && has_any(t, {"it", "that"})) {
    return true;
  }
  return false;
}

bool looks_like_light(std::string_view t) {
  return has_any(t, {"light", "lights", "lamp", "lamps"});
}

std::string extract_room(std::string_view t) {
  static const char* rooms[] = {"living room", "dining room", "bed room",
                                "kitchen",     "lounge",      "bedroom",
                                "hallway",     "hall",        "office",
                                "bathroom",    "garage",      "porch",
                                "studio",      "den"};
  for (const char* r : rooms) {
    if (has_word(t, r) || t.find(r) != std::string_view::npos) return r;
  }
  return {};
}

bool wants_on(std::string_view t) {
  if (has_word(t, "off") || has_word(t, "kill") || has_word(t, "dark")) return false;
  if (has_word(t, "on") || has_word(t, "enable")) return true;
  return true;
}

bool wants_off(std::string_view t) {
  return has_word(t, "off") || has_word(t, "kill") || has_word(t, "dark") ||
         has_word(t, "disable");
}

bool wants_toggle(std::string_view t) {
  return has_word(t, "toggle") ||
         (has_word(t, "switch") && !has_word(t, "on") && !has_word(t, "off"));
}

}  // namespace

const char* home_intent_kind_name(HomeIntentKind kind) {
  switch (kind) {
    case HomeIntentKind::Timer:
      return "timer";
    case HomeIntentKind::LightOn:
      return "light_on";
    case HomeIntentKind::LightOff:
      return "light_off";
    case HomeIntentKind::LightToggle:
      return "light_toggle";
    case HomeIntentKind::VolumeUp:
      return "volume_up";
    case HomeIntentKind::VolumeDown:
      return "volume_down";
    case HomeIntentKind::Weather:
      return "weather";
    case HomeIntentKind::NextAlarm:
      return "alarm";
  }
  return "home";
}

std::optional<HomeIntent> parse_home_intent(std::string_view transcript) {
  const std::string t = fold_phatic(transcript);
  if (t.empty()) return std::nullopt;

  if (looks_like_timer(t)) {
    HomeIntent in;
    in.kind = HomeIntentKind::Timer;
    in.timer_seconds = parse_duration_seconds(t);
    return in;
  }

  if (looks_like_alarm(t) && !looks_like_timer(t)) {
    HomeIntent in;
    in.kind = HomeIntentKind::NextAlarm;
    return in;
  }

  if (looks_like_weather(t)) {
    if (t.find(" in ") != std::string::npos) return std::nullopt;
    HomeIntent in;
    in.kind = HomeIntentKind::Weather;
    return in;
  }

  if (looks_like_volume(t) && !looks_like_light(t)) {
    HomeIntent in;
    in.kind = HomeIntentKind::VolumeDown;
    if (has_any(t, {"up", "louder"})) in.kind = HomeIntentKind::VolumeUp;
    return in;
  }

  if (looks_like_light(t)) {
    HomeIntent in;
    in.room = extract_room(t);
    if (wants_toggle(t)) {
      in.kind = HomeIntentKind::LightToggle;
    } else if (wants_off(t)) {
      in.kind = HomeIntentKind::LightOff;
    } else if (wants_on(t) || has_word(t, "turn") || has_word(t, "put")) {
      in.kind = HomeIntentKind::LightOn;
    } else {
      return std::nullopt;
    }
    return in;
  }

  return std::nullopt;
}

std::string spoken_number(int n) {
  static const char* under[] = {
      "zero",        "one",         "two",           "three",
      "four",        "five",        "six",           "seven",
      "eight",       "nine",        "ten",           "eleven",
      "twelve",      "thirteen",    "fourteen",      "fifteen",
      "sixteen",     "seventeen",   "eighteen",      "nineteen"};
  if (n < 0) return "minus " + spoken_number(-n);
  if (n < 20) return under[n];
  static const char* tens[] = {"",      "",      "twenty", "thirty", "forty",
                               "fifty", "sixty", "seventy", "eighty", "ninety"};
  if (n < 100) {
    std::string s = tens[n / 10];
    if (n % 10) {
      s.push_back(' ');
      s += under[n % 10];
    }
    return s;
  }
  if (n < 1000) {
    std::string s = spoken_number(n / 100) + " hundred";
    if (n % 100) {
      s += " and ";
      s += spoken_number(n % 100);
    }
    return s;
  }
  std::ostringstream oss;
  oss << n;
  return oss.str();
}

std::string spoken_duration(int seconds) {
  if (seconds <= 0) return {};
  if (seconds % 3600 == 0) {
    const int h = seconds / 3600;
    return spoken_number(h) + (h == 1 ? " hour" : " hours");
  }
  if (seconds % 60 == 0) {
    const int m = seconds / 60;
    return spoken_number(m) + (m == 1 ? " minute" : " minutes");
  }
  return spoken_number(seconds) + (seconds == 1 ? " second" : " seconds");
}

}  // namespace intercom
