#pragma once

#include <string>
#include <unordered_map>

namespace intercom {

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
  // >1 slows phonemes (smoother HAL). <1 is snappier / choppier.
  double length_scale = 1.18;
  double noise_scale = 0.45;
  double noise_w = 0.5;
  double sentence_silence = 0.22;
};

struct KokoroConfig {
  std::string binary = "kokoro-tts";
  std::string voice = "af_heart";
  double speed = 1.0;
  std::string model;
  std::string voices;
};

struct Config {
  std::string listen_host = "127.0.0.1";
  int listen_port = 8090;
  std::string device_token;
  std::unordered_map<std::string, std::string> devices;  // device_id -> token
  std::string arbiter_base_url = "http://127.0.0.1:8080";
  std::string arbiter_token;
  std::string agent = "arthur";
  // Inline agent constitution snapshotted into Arbiter on conversation create.
  // Empty → {config_dir}/arthur.agent.json when agent != "index".
  std::string agent_def_path;
  std::string agent_def_json;
  std::string session_db = "~/.intercom/sessions.db";
  int sample_rate = 16000;
  int channels = 1;
  bool fast_path = true;
  std::string tts_provider = "piper";
  WhisperConfig whisper;
  PiperConfig piper;
  KokoroConfig kokoro;

  static Config load(const std::string& path);

  bool authorize_device(const std::string& device_id,
                        const std::string& bearer_token) const;
};

}  // namespace intercom
