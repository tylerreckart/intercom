#pragma once

#include "intercom/config.hpp"

#include <atomic>
#include <optional>
#include <string>

namespace intercom {

enum class FillerStage {
  Initial,
  FollowUp,
};

// Lightweight LLM client for brief spoken "thinking aloud" phrases during silence.
class FillerClient {
 public:
  explicit FillerClient(FillerConfig config);

  bool enabled() const { return config_.enabled && !config_.api_key.empty(); }

  // Ultra-short local ack — no network, for immediate playback.
  static std::string instant_ack();

  // Returns empty on failure, disabled, or cancel.
  std::string generate(const std::string& transcript,
                       FillerStage stage,
                       const std::string& previous_phrase,
                       std::atomic<bool>* cancel_flag,
                       std::string* err) const;

 private:
  FillerConfig config_;
};

}  // namespace intercom
