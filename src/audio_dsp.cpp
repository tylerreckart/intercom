#include "intercom/audio_dsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace intercom {
namespace {

constexpr double kPi = 3.14159265358979323846;

double db_to_gain(double db) {
  return std::pow(10.0, db / 20.0);
}

double time_coefficient(double milliseconds, int sample_rate) {
  if (milliseconds <= 0.0 || sample_rate <= 0) return 0.0;
  return std::exp(-1.0 / (milliseconds * 0.001 * sample_rate));
}

}  // namespace

SpeechDspProcessor::SpeechDspProcessor(SpeechDspConfig config,
                                       int sample_rate,
                                       double delivery_gain_db)
    : config_(std::move(config)),
      sample_rate_(std::max(8000, sample_rate)) {
  output_gain_ = db_to_gain(config_.makeup_db + delivery_gain_db);
  limiter_ = std::clamp(db_to_gain(config_.limiter_db), 0.1, 1.0);

  const double highpass =
      std::clamp(config_.highpass_hz, 0.0, sample_rate_ * 0.45);
  if (highpass > 0.0) {
    const double dt = 1.0 / sample_rate_;
    const double rc = 1.0 / (2.0 * kPi * highpass);
    hp_alpha_ = rc / (rc + dt);
  }

  const double presence =
      std::clamp(config_.presence_hz, 20.0, sample_rate_ * 0.45);
  const double q = std::max(0.1, config_.presence_q);
  if (std::abs(config_.presence_db) > 0.01) {
    const double a = std::pow(10.0, config_.presence_db / 40.0);
    const double omega = 2.0 * kPi * presence / sample_rate_;
    const double alpha = std::sin(omega) / (2.0 * q);
    const double cos_omega = std::cos(omega);
    const double a0 = 1.0 + alpha / a;
    b0_ = (1.0 + alpha * a) / a0;
    b1_ = (-2.0 * cos_omega) / a0;
    b2_ = (1.0 - alpha * a) / a0;
    a1_ = (-2.0 * cos_omega) / a0;
    a2_ = (1.0 - alpha / a) / a0;
  }

  attack_coeff_ =
      time_coefficient(std::max(0.1, config_.compressor_attack_ms), sample_rate_);
  release_coeff_ =
      time_coefficient(std::max(1.0, config_.compressor_release_ms), sample_rate_);
}

double SpeechDspProcessor::process_sample(double input) {
  if (!config_.enabled) return std::clamp(input, -1.0, 1.0);

  double sample = input;
  if (hp_alpha_ > 0.0) {
    const double filtered = hp_alpha_ * (hp_y1_ + sample - hp_x1_);
    hp_x1_ = sample;
    hp_y1_ = filtered;
    sample = filtered;
  }

  const double equalized =
      b0_ * sample + b1_ * eq_x1_ + b2_ * eq_x2_ -
      a1_ * eq_y1_ - a2_ * eq_y2_;
  eq_x2_ = eq_x1_;
  eq_x1_ = sample;
  eq_y2_ = eq_y1_;
  eq_y1_ = equalized;
  sample = equalized;

  const double level = std::abs(sample);
  const double env_coeff = level > envelope_ ? attack_coeff_ : release_coeff_;
  envelope_ = env_coeff * envelope_ + (1.0 - env_coeff) * level;

  const double threshold =
      std::clamp(db_to_gain(config_.compressor_threshold_db), 0.001, 1.0);
  const double ratio = std::max(1.0, config_.compressor_ratio);
  double target_gain = 1.0;
  if (envelope_ > threshold) {
    target_gain =
        std::pow(envelope_ / threshold, (1.0 / ratio) - 1.0);
  }
  const double gain_coeff =
      target_gain < compressor_gain_ ? attack_coeff_ : release_coeff_;
  compressor_gain_ =
      gain_coeff * compressor_gain_ + (1.0 - gain_coeff) * target_gain;

  sample *= compressor_gain_ * output_gain_;
  return std::clamp(sample, -limiter_, limiter_);
}

std::vector<std::uint8_t> SpeechDspProcessor::process(
    const std::uint8_t* data,
    std::size_t len) {
  if (!data || len == 0) return {};
  std::vector<std::uint8_t> input;
  input.reserve(len + (has_odd_byte_ ? 1 : 0));
  if (has_odd_byte_) {
    input.push_back(odd_byte_);
    has_odd_byte_ = false;
  }
  input.insert(input.end(), data, data + len);
  if (input.size() % 2 != 0) {
    odd_byte_ = input.back();
    has_odd_byte_ = true;
    input.pop_back();
  }
  if (!config_.enabled) return input;

  std::vector<std::uint8_t> output(input.size());
  for (std::size_t i = 0; i < input.size(); i += 2) {
    std::int16_t raw = 0;
    std::memcpy(&raw, input.data() + i, sizeof(raw));
    const double normalized = static_cast<double>(raw) / 32768.0;
    const double processed = process_sample(normalized);
    const auto value = static_cast<std::int16_t>(
        std::lround(processed * 32767.0));
    std::memcpy(output.data() + i, &value, sizeof(value));
  }
  return output;
}

}  // namespace intercom
