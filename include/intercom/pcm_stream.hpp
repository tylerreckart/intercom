#pragma once

#include "intercom/turn_pipeline.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace intercom {

// Thread-safe queue bridging pipeline TTS output to HTTP chunked responses.
class PcmStream {
 public:
  void push(const std::uint8_t* data, std::size_t len) {
    if (len == 0) return;
    std::lock_guard<std::mutex> lk(mu_);
    chunks_.emplace_back(data, data + len);
    cv_.notify_one();
  }

  void finish() {
    std::lock_guard<std::mutex> lk(mu_);
    closed_ = true;
    cv_.notify_one();
  }

  // Blocks until data is available or the stream is closed. Empty vector means EOF.
  std::vector<std::uint8_t> wait_pop() {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [&] { return !chunks_.empty() || closed_; });
    if (chunks_.empty()) return {};
    auto chunk = std::move(chunks_.front());
    chunks_.pop_front();
    return chunk;
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::vector<std::uint8_t>> chunks_;
  bool closed_ = false;
};

class StreamingAudioSink : public AudioSink {
 public:
  explicit StreamingAudioSink(PcmStream& stream) : stream_(stream) {}

  bool write(const std::uint8_t* data, std::size_t len) override {
    stream_.push(data, len);
    return true;
  }

 private:
  PcmStream& stream_;
};

}  // namespace intercom
