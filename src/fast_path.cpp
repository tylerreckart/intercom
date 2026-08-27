#include "intercom/fast_path.hpp"
#include "intercom/clock.hpp"
#include "intercom/util.hpp"

#include <random>
#include <string_view>
#include <vector>

namespace intercom {
namespace {

bool is_punct(char c) {
  return c == '.' || c == '!' || c == '?' || c == ',' || c == ';' || c == ':';
}

std::string fold_phatic(std::string_view raw) {
  std::string t = to_lower(trim(raw));
  std::string out;
  out.reserve(t.size());
  for (char c : t) {
    if (is_punct(c) || c == '\'') {
      if (c == '\'') continue;
      out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  out = trim(out);
  std::vector<std::string> tok;
  std::string cur;
  for (char c : out) {
    if (c == ' ' || c == '\t') {
      if (!cur.empty()) {
        tok.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) tok.push_back(cur);

  auto drop = [](const std::string& w) {
    return w == "arthur" || w == "please" || w == "sir";
  };
  while (!tok.empty() && drop(tok.front())) tok.erase(tok.begin());
  while (!tok.empty() && drop(tok.back())) tok.pop_back();

  std::string joined;
  for (const auto& w : tok) {
    if (!joined.empty()) joined.push_back(' ');
    joined += w;
  }
  return joined;
}

bool eq(std::string_view a, std::initializer_list<const char*> opts) {
  for (const char* o : opts) {
    if (a == o) return true;
  }
  return false;
}

std::string pick(std::initializer_list<const char*> phrases) {
  static thread_local std::mt19937 rng{std::random_device{}()};
  static thread_local std::string last;
  std::vector<const char*> opts(phrases);
  if (opts.empty()) return {};
  std::uniform_int_distribution<std::size_t> dist(0, opts.size() - 1);
  std::size_t i = dist(rng);
  if (opts.size() > 1 && opts[i] == last) i = (i + 1) % opts.size();
  last = opts[i];
  return last;
}

int local_hour() { return local_now().tm_hour; }

std::string hello_by_hour() {
  const int h = local_hour();
  if (h < 5 || h >= 22) {
    return pick({"Good evening, sir. What can I do for you?",
                 "Evening, sir. How can I help?"});
  }
  if (h < 12) {
    return pick({"Good morning, sir. What can I do for you?",
                 "Morning, sir. How can I help?",
                 "Good morning, sir. What's on your mind?"});
  }
  if (h < 17) {
    return pick({"Good afternoon, sir. What can I do for you?",
                 "Afternoon, sir. How can I help?"});
  }
  return pick({"Good evening, sir. What can I do for you?",
               "Evening, sir. How can I help?"});
}

bool social_folded(std::string_view t) {
  if (t.empty()) return true;
  return eq(t, {"good morning", "morning", "good afternoon", "afternoon",
                "good evening", "evening", "good night", "goodnight", "goodbye",
                "bye", "see you", "farewell", "hello", "hi", "hey", "ping",
                "hello there", "hi there", "hey there", "how are you",
                "how are you doing", "hows it going", "how is it going",
                "you alright", "you all right", "thanks", "thank you", "cheers",
                "ta", "status", "are you there", "you there", "you around"});
}

bool clock_folded(std::string_view t) {
  return eq(t, {"time", "the time", "what time", "what time is it",
                "whats the time", "what is the time", "current time",
                "date", "the date", "what date", "whats the date",
                "what is the date", "whats today", "what is today",
                "what day is it", "whats the day", "what day"});
}

}  // namespace

bool is_social_turn(std::string_view transcript) {
  return social_folded(fold_phatic(transcript));
}

bool is_clock_query(std::string_view transcript) {
  return clock_folded(fold_phatic(transcript));
}

bool withholds_fillers(std::string_view transcript) {
  return is_social_turn(transcript) || is_clock_query(transcript);
}

FastPath::FastPath(bool enabled) : enabled_(enabled) {}

std::optional<FastPathResult> FastPath::try_handle(const std::string& transcript) const {
  if (!enabled_) return std::nullopt;
  const std::string raw = to_lower(trim(transcript));
  if (raw.empty()) return std::nullopt;

  if (raw.rfind("echo ", 0) == 0) {
    return FastPathResult{trim(transcript.substr(5))};
  }

  if (is_clock_query(transcript)) {
    const std::string t = fold_phatic(transcript);
    if (t.find("date") != std::string::npos || t.find("day") != std::string::npos ||
        t.find("today") != std::string::npos) {
      return FastPathResult{spoken_date_now()};
    }
    return FastPathResult{spoken_time_now()};
  }

  const std::string t = fold_phatic(transcript);
  if (t.empty()) {
    return FastPathResult{hello_by_hour()};
  }

  if (eq(t, {"good morning", "morning"})) {
    return FastPathResult{pick({"Good morning, sir. What can I do for you?",
                                "Morning, sir. How can I help?",
                                "Good morning, sir. What's on your mind?"})};
  }
  if (eq(t, {"good afternoon", "afternoon"})) {
    return FastPathResult{pick({"Good afternoon, sir. What can I do for you?",
                                "Afternoon, sir. How can I help?"})};
  }
  if (eq(t, {"good evening", "evening"})) {
    return FastPathResult{pick({"Good evening, sir. What can I do for you?",
                                "Evening, sir. How can I help?"})};
  }
  if (eq(t, {"good night", "goodnight"})) {
    return FastPathResult{pick({"Good night, sir.", "Sleep well, sir."})};
  }
  if (eq(t, {"goodbye", "bye", "see you", "farewell"})) {
    return FastPathResult{pick({"Goodbye, sir.", "Until later, sir."})};
  }
  if (eq(t, {"hello", "hi", "hey", "ping", "hello there", "hi there", "hey there"})) {
    return FastPathResult{hello_by_hour()};
  }
  if (eq(t, {"how are you", "how are you doing", "hows it going", "how is it going",
             "you alright", "you all right"})) {
    return FastPathResult{pick({"Very well, sir, thank you. What can I do for you?",
                                "I'm well, sir. How can I help?",
                                "All good, sir. What do you need?"})};
  }
  if (eq(t, {"thanks", "thank you", "cheers", "ta"})) {
    return FastPathResult{pick({"You're welcome, sir. Anything else?",
                                "Glad to, sir. What else can I do?"})};
  }
  if (eq(t, {"status", "are you there", "you there", "you around"})) {
    return FastPathResult{pick({"Right here, sir. What do you need?",
                                "I'm here, sir. How can I help?"})};
  }

  return std::nullopt;
}

}  // namespace intercom
