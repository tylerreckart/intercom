#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace intercom {

// Speech-to-text provider. Transport-agnostic (HTTP PTT today, WS later).
class SttProvider {
 public:
  virtual ~SttProvider() = default;

  // Transcribe mono s16le PCM. Returns empty string on failure; error in *err.
  virtual std::string transcribe(const std::vector<std::uint8_t>& pcm,
                                 int sample_rate,
                                 int channels,
                                 std::string* err) = 0;

  virtual bool ready(std::string* detail) const = 0;
};

}  // namespace intercom
