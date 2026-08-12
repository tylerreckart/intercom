#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace alfred {

// Text-to-speech provider. Emits mono s16le PCM at Alfred's target sample rate.
class TtsProvider {
 public:
  using PcmChunkFn = std::function<bool(const std::uint8_t* data, std::size_t len)>;

  virtual ~TtsProvider() = default;

  // Synthesize text; invoke on_chunk with PCM. Return false from on_chunk to abort.
  // Output sample rate must match Alfred config (typically 16 kHz).
  virtual bool synthesize(const std::string& text,
                          PcmChunkFn on_chunk,
                          std::string* err) = 0;

  virtual bool ready(std::string* detail) const = 0;
};

}  // namespace alfred
