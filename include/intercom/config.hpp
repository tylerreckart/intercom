#pragma once

#include <string>
#include <unordered_map>

namespace intercom {

struct WhisperConfig {
  std::string binary = "whisper-cli";
  std::string model;
  int timeout_seconds = 120;
  std::string language = "en";
  bool use_server = true;
  std::string server_binary;
  std::string server_url;
  int server_port = 8092;
};

struct KokoroConfig {
  std::string binary = "kokoro-tts";
  std::string voice = "af_heart";
  double speed = 1.0;
  std::string model;
  std::string voices;
  bool use_server = true;
  std::string server_script;
  std::string server_url;
  int server_port = 8091;
};

struct FillerConfig {
  bool enabled = true;
  // OpenAI-compatible chat completions endpoint (OpenRouter or DeepSeek direct).
  std::string api_base_url = "https://openrouter.ai/api/v1";
  std::string api_key;
  std::string model = "deepseek/deepseek-v4-flash";
  // Speak a local backchannel this many ms after the turn starts (0 = disabled).
  // High enough that a fast Arbiter reply skips filler entirely.
  int instant_ack_ms = 700;
  // Earliest ms to speak an LLM phrase when instant ack is off.
  int min_silence_ms = 1100;
  int followup_silence_ms = 8000;
  int max_followups = 0;
  int timeout_ms = 2500;
  int max_tokens = 16;
  double temperature = 0.6;
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
