#pragma once

#include "intercom/config.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace intercom {

// Stateful mono s16le speech processor. State carries across HTTP chunks so
// streamed and complete-WAV synthesis sound the same.
class SpeechDspProcessor {
 public:
  SpeechDspProcessor(SpeechDspConfig config,
                     int sample_rate,
                     double delivery_gain_db = 0.0);

  std::vector<std::uint8_t> process(const std::uint8_t* data, std::size_t len);
  std::vector<std::uint8_t> process(const std::vector<std::uint8_t>& pcm) {
    return process(pcm.data(), pcm.size());
  }

 private:
  double process_sample(double input);

  SpeechDspConfig config_;
  int sample_rate_ = 24000;
  double output_gain_ = 1.0;
  double limiter_ = 0.891;

  double hp_alpha_ = 0.0;
  double hp_x1_ = 0.0;
  double hp_y1_ = 0.0;

  double b0_ = 1.0;
  double b1_ = 0.0;
  double b2_ = 0.0;
  double a1_ = 0.0;
  double a2_ = 0.0;
  double eq_x1_ = 0.0;
  double eq_x2_ = 0.0;
  double eq_y1_ = 0.0;
  double eq_y2_ = 0.0;

  double envelope_ = 0.0;
  double compressor_gain_ = 1.0;
  double attack_coeff_ = 0.0;
  double release_coeff_ = 0.0;
  bool has_odd_byte_ = false;
  std::uint8_t odd_byte_ = 0;
};

}  // namespace intercom
