#pragma once

#include "intercom/config.hpp"
#include "intercom/stt.hpp"

#include <string>

namespace intercom {

class WhisperStt : public SttProvider {
 public:
  explicit WhisperStt(WhisperConfig cfg);

  std::string transcribe(const std::vector<std::uint8_t>& pcm,
                         int sample_rate,
                         int channels,
                         std::string* err) override;

  bool ready(std::string* detail) const override;

 private:
  WhisperConfig cfg_;
};

}  // namespace intercom
