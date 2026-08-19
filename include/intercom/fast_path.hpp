#pragma once

#include <optional>
#include <string>

namespace intercom {

struct FastPathResult {
  std::string reply;
};

class FastPath {
 public:
  explicit FastPath(bool enabled);

  // Returns a local reply when the utterance should skip Arbiter.
  std::optional<FastPathResult> try_handle(const std::string& transcript) const;

 private:
  bool enabled_;
};

}  // namespace intercom
