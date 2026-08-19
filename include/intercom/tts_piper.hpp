#pragma once

#include "intercom/config.hpp"
#include "intercom/tts.hpp"

namespace intercom {

class PiperTts : public TtsProvider {
 public:
  PiperTts(PiperConfig cfg, int target_sample_rate);

  bool synthesize(const std::string& text,
                  PcmChunkFn on_chunk,
                  std::string* err) override;

  bool ready(std::string* detail) const override;

 private:
  PiperConfig cfg_;
  int target_sample_rate_;
};

}  // namespace intercom
