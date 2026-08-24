#pragma once

#include "intercom/config.hpp"
#include "intercom/stt.hpp"

#include <memory>
#include <mutex>
#include <string>

namespace intercom {

class ManagedServer;

class WhisperStt : public SttProvider {
 public:
  explicit WhisperStt(WhisperConfig cfg);
  ~WhisperStt() override;

  WhisperStt(const WhisperStt&) = delete;
  WhisperStt& operator=(const WhisperStt&) = delete;

  std::string transcribe(const std::vector<std::uint8_t>& pcm,
                         int sample_rate,
                         int channels,
                         std::string* err) override;

  bool ready(std::string* detail) const override;

 private:
  std::string transcribe_http(const std::vector<std::uint8_t>& pcm,
                              int sample_rate,
                              int channels,
                              std::string* err);
  std::string transcribe_cli(const std::vector<std::uint8_t>& pcm,
                             int sample_rate,
                             int channels,
                             std::string* err);
  bool ensure_server(std::string* err);

  WhisperConfig cfg_;
  std::string server_url_;
  std::unique_ptr<ManagedServer> child_;
  mutable std::mutex mu_;
};

}  // namespace intercom
