#include "intercom/clock.hpp"

#include <chrono>
#include <ctime>
#include <sstream>
#include <string>

namespace intercom {
namespace {

const char* weekday_name(int wday) {
  static const char* k[] = {"Sunday",    "Monday",   "Tuesday", "Wednesday",
                            "Thursday",  "Friday",   "Saturday"};
  if (wday < 0 || wday > 6) return "today";
  return k[wday];
}

const char* month_name(int mon) {
  static const char* k[] = {"January",   "February", "March",    "April",
                            "May",       "June",     "July",     "August",
                            "September", "October",  "November", "December"};
  if (mon < 0 || mon > 11) return "";
  return k[mon];
}

const char* day_ordinal(int d) {
  static const char* k[] = {"",
                            "first",        "second",      "third",
                            "fourth",       "fifth",       "sixth",
                            "seventh",      "eighth",      "ninth",
                            "tenth",        "eleventh",    "twelfth",
                            "thirteenth",   "fourteenth",  "fifteenth",
                            "sixteenth",    "seventeenth", "eighteenth",
                            "nineteenth",   "twentieth",   "twenty first",
                            "twenty second","twenty third","twenty fourth",
                            "twenty fifth", "twenty sixth","twenty seventh",
                            "twenty eighth","twenty ninth","thirtieth",
                            "thirty first"};
  if (d < 1 || d > 31) return "";
  return k[d];
}

const char* hour_word(int hour24) {
  static const char* k[] = {"twelve", "one", "two",   "three", "four",   "five",
                            "six",    "seven", "eight", "nine",  "ten",    "eleven"};
  return k[hour24 % 12];
}

std::string minute_words(int m) {
  static const char* under_20[] = {"",          "one",      "two",      "three",
                                   "four",      "five",     "six",      "seven",
                                   "eight",     "nine",     "ten",      "eleven",
                                   "twelve",    "thirteen", "fourteen", "fifteen",
                                   "sixteen",   "seventeen","eighteen", "nineteen"};
  static const char* tens[] = {"", "", "twenty", "thirty", "forty", "fifty"};
  if (m == 0) return "o'clock";
  if (m < 10) return std::string("oh ") + under_20[m];
  if (m < 20) return under_20[m];
  std::string s = tens[m / 10];
  if (m % 10) {
    s.push_back(' ');
    s += under_20[m % 10];
  }
  return s;
}

const char* day_part(int hour) {
  if (hour < 5 || hour >= 22) return "night";
  if (hour < 12) return "morning";
  if (hour < 17) return "afternoon";
  return "evening";
}

}  // namespace

std::tm local_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t tt = std::chrono::system_clock::to_time_t(now);
  std::tm local {};
#if defined(_WIN32)
  localtime_s(&local, &tt);
#else
  tzset();
  localtime_r(&tt, &local);
#endif
  return local;
}

std::string local_clock_rule() {
  const std::tm t = local_now();
  char stamp[64];
  std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M %Z", &t);
  char offset[16];
  std::strftime(offset, sizeof(offset), "%z", &t);
  std::ostringstream oss;
  oss << "CURRENT LOCAL DATETIME: " << weekday_name(t.tm_wday) << ", "
      << t.tm_mday << ' ' << month_name(t.tm_mon) << ' ' << (t.tm_year + 1900)
      << ", " << stamp << " (" << offset << "). Today is "
      << weekday_name(t.tm_wday) << ". It is " << day_part(t.tm_hour)
      << ". This is the current local time — never look it up with tools, "
         "search, or exec.";
  return oss.str();
}

std::string spoken_time_now() {
  const std::tm t = local_now();
  std::ostringstream oss;
  oss << "It's " << hour_word(t.tm_hour);
  const std::string mins = minute_words(t.tm_min);
  if (mins == "o'clock") oss << " o'clock";
  else oss << ' ' << mins;
  oss << ", sir.";
  return oss.str();
}

std::string spoken_date_now() {
  const std::tm t = local_now();
  std::ostringstream oss;
  oss << "It's " << weekday_name(t.tm_wday) << " the " << day_ordinal(t.tm_mday)
      << " of " << month_name(t.tm_mon) << ", sir.";
  return oss.str();
}

}  // namespace intercom
