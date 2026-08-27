#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace intercom {

struct FastPathResult {
  std::string reply;
};

// Social greetings, thanks, and presence checks — not a reason to "look something up".
bool is_social_turn(std::string_view transcript);
// Time or date questions that should use the local clock, not tools.
bool is_clock_query(std::string_view transcript);
// True when fillers would talk over a greeting or a clock answer.
bool withholds_fillers(std::string_view transcript);

class FastPath {
 public:
  explicit FastPath(bool enabled);

  // Returns a local reply when the utterance should skip Arbiter.
  std::optional<FastPathResult> try_handle(const std::string& transcript) const;

 private:
  bool enabled_;
};

}  // namespace intercom
