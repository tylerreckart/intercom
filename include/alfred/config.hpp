#pragma once

#include <string>
#include <unordered_map>

namespace alfred {

struct WhisperConfig {
  std::string binary = "whisper-cli";
  std::string model;
  int timeout_seconds = 120;
  std::string language = "en";
};

struct PiperConfig {
  std::string binary = "piper";
  std::string model;
  int timeout_seconds = 60;
  int native_sample_rate = 22050;
};

struct Config {
  std::string listen_host = "127.0.0.1";
  int listen_port = 8090;
  std::string device_token;
  std::unordered_map<std::string, std::string> devices;  // device_id -> token
  std::string arbiter_base_url = "http://127.0.0.1:8080";
  std::string arbiter_token;
  std::string agent = "index";
  std::string session_db = "~/.alfred/sessions.db";
  int sample_rate = 16000;
  int channels = 1;
  bool fast_path = true;
  WhisperConfig whisper;
  PiperConfig piper;

  static Config load(const std::string& path);

  bool authorize_device(const std::string& device_id,
                        const std::string& bearer_token) const;
};

}  // namespace alfred
