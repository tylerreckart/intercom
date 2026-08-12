#include "alfred/fast_path.hpp"
#include "alfred/util.hpp"

#include <chrono>
#include <ctime>
#include <sstream>

namespace alfred {

FastPath::FastPath(bool enabled) : enabled_(enabled) {}

std::optional<FastPathResult> FastPath::try_handle(const std::string& transcript) const {
  if (!enabled_) return std::nullopt;
  const std::string t = to_lower(trim(transcript));
  if (t.empty()) return std::nullopt;

  if (t == "ping" || t == "hello" || t == "hi" || t == "hey alfred" || t == "hey") {
    return FastPathResult{"Hello. Alfred is ready."};
  }

  if (t == "status" || t == "are you there" || t == "you there") {
    return FastPathResult{"Alfred online. Speech bridge is healthy."};
  }

  if (t.find("what time") != std::string::npos || t == "time" ||
      t.find("current time") != std::string::npos) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm local {};
#if defined(_WIN32)
    localtime_s(&local, &tt);
#else
    localtime_r(&tt, &local);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%I:%M %p", &local);
    std::ostringstream oss;
    oss << "It is " << buf << ".";
    return FastPathResult{oss.str()};
  }

  if (t.rfind("echo ", 0) == 0) {
    return FastPathResult{trim(transcript.substr(5))};
  }

  return std::nullopt;
}

}  // namespace alfred
