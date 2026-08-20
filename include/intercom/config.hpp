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

struct KokoroConfig {
  std::string binary = "kokoro-tts";
  std::string voice = "af_heart";
  double speed = 1.0;
  std::string model;
  std::string voices;
};

struct FillerConfig {
  bool enabled = true;
  // OpenAI-compatible chat completions endpoint (OpenRouter or DeepSeek direct).
  std::string api_base_url = "https://openrouter.ai/api/v1";
  std::string api_key;
  std::string model = "deepseek/deepseek-v4-flash";
  // Speak a local ack this many ms after the turn starts (0 = disabled).
  int instant_ack_ms = 400;
  // Earliest ms to speak the LLM contextual phrase (unless instant ack already played).
  int min_silence_ms = 350;
  int followup_silence_ms = 6000;
  int max_followups = 1;
  int timeout_ms = 2500;
  int max_tokens = 24;
  double temperature = 0.7;
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
  WhisperConfig whisper;
  KokoroConfig kokoro;
  FillerConfig filler;

  static Config load(const std::string& path);

  bool authorize_device(const std::string& device_id,
                        const std::string& bearer_token) const;
};

}  // namespace intercom
