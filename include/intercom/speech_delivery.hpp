#pragma once

#include <string_view>

namespace intercom {

enum class SpeechDelivery {
  Neutral,
  Warm,
  Subdued,
  Firm,
};

SpeechDelivery classify_speech_delivery(std::string_view text);
double delivery_speed_multiplier(SpeechDelivery delivery);
double delivery_gain_db(SpeechDelivery delivery);
int delivery_pause_adjustment_ms(SpeechDelivery delivery);

}  // namespace intercom
