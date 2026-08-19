#include "intercom/config.hpp"
#include "intercom/util.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace intercom {
namespace {

namespace fs = std::filesystem;

std::string require_string(const nlohmann::json& j, const char* key, const std::string& def = {}) {
  if (!j.contains(key) || j[key].is_null()) return def;
  if (!j[key].is_string()) {
    throw std::runtime_error(std::string("config field '") + key + "' must be a string");
  }
  return j[key].get<std::string>();
}

int require_int(const nlohmann::json& j, const char* key, int def) {
  if (!j.contains(key) || j[key].is_null()) return def;
  if (!j[key].is_number_integer()) {
    throw std::runtime_error(std::string("config field '") + key + "' must be an integer");
  }
  return j[key].get<int>();
}

double require_number(const nlohmann::json& j, const char* key, double def) {
  if (!j.contains(key) || j[key].is_null()) return def;
  if (!j[key].is_number()) {
    throw std::runtime_error(std::string("config field '") + key + "' must be a number");
  }
  return j[key].get<double>();
}

bool require_bool(const nlohmann::json& j, const char* key, bool def) {
  if (!j.contains(key) || j[key].is_null()) return def;
  if (!j[key].is_boolean()) {
    throw std::runtime_error(std::string("config field '") + key + "' must be a boolean");
  }
  return j[key].get<bool>();
}

std::string load_agent_def_json(const fs::path& config_path,
                                const std::string& agent,
                                const std::string& agent_def_path) {
  if (agent == "index") return {};

  fs::path path = agent_def_path.empty()
                      ? config_path.parent_path() / "arthur.agent.json"
                      : fs::path(agent_def_path);
  if (!path.is_absolute()) {
    path = config_path.parent_path() / path;
  }
  path = fs::path(expand_home(path.string()));

  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("failed to open agent_def: " + path.string());
  }
  nlohmann::json def;
  in >> def;
  if (!def.is_object()) {
    throw std::runtime_error("agent_def root must be a JSON object");
  }
  if (!def.contains("id") || !def["id"].is_string()) {
    throw std::runtime_error("agent_def must contain string field 'id'");
  }
  if (def["id"].get<std::string>() != agent) {
    throw std::runtime_error("agent_def id '" + def["id"].get<std::string>() +
                             "' does not match config agent '" + agent + "'");
  }
  return def.dump();
}

}  // namespace

Config Config::load(const std::string& path) {
  const fs::path config_path = fs::path(path).lexically_normal();
  std::ifstream in(config_path);
  if (!in) {
    throw std::runtime_error("failed to open config: " + path);
  }
  nlohmann::json j;
  in >> j;
  if (!j.is_object()) {
    throw std::runtime_error("config root must be a JSON object");
  }

  Config c;
  c.listen_host = require_string(j, "listen_host", c.listen_host);
  c.listen_port = require_int(j, "listen_port", c.listen_port);
  c.device_token = require_string(j, "device_token", "");
  c.arbiter_base_url = require_string(j, "arbiter_base_url", c.arbiter_base_url);
  c.arbiter_token = require_string(j, "arbiter_token", "");
  c.agent = require_string(j, "agent", c.agent);
  c.agent_def_path = require_string(j, "agent_def", c.agent_def_path);
  c.agent_def_json =
      load_agent_def_json(config_path, c.agent, c.agent_def_path);
  c.session_db = expand_home(require_string(j, "session_db", c.session_db));
  c.sample_rate = require_int(j, "sample_rate", c.sample_rate);
  c.channels = require_int(j, "channels", c.channels);
  c.fast_path = require_bool(j, "fast_path", c.fast_path);
  c.tts_provider = require_string(j, "tts_provider", c.tts_provider);

  if (j.contains("devices") && j["devices"].is_object()) {
    for (auto it = j["devices"].begin(); it != j["devices"].end(); ++it) {
      if (it.value().is_string()) {
        c.devices[it.key()] = it.value().get<std::string>();
      }
    }
  }

  if (j.contains("whisper") && j["whisper"].is_object()) {
    const auto& w = j["whisper"];
    c.whisper.binary = require_string(w, "binary", c.whisper.binary);
    c.whisper.model = expand_home(require_string(w, "model", c.whisper.model));
    c.whisper.timeout_seconds = require_int(w, "timeout_seconds", c.whisper.timeout_seconds);
    c.whisper.language = require_string(w, "language", c.whisper.language);
  }

  if (j.contains("piper") && j["piper"].is_object()) {
    const auto& p = j["piper"];
    c.piper.binary = require_string(p, "binary", c.piper.binary);
    c.piper.model = expand_home(require_string(p, "model", c.piper.model));
    c.piper.timeout_seconds = require_int(p, "timeout_seconds", c.piper.timeout_seconds);
    c.piper.native_sample_rate =
        require_int(p, "native_sample_rate", c.piper.native_sample_rate);
    c.piper.length_scale = require_number(p, "length_scale", c.piper.length_scale);
    c.piper.noise_scale = require_number(p, "noise_scale", c.piper.noise_scale);
    c.piper.noise_w = require_number(p, "noise_w", c.piper.noise_w);
    c.piper.sentence_silence =
        require_number(p, "sentence_silence", c.piper.sentence_silence);
  }

  if (j.contains("kokoro") && j["kokoro"].is_object()) {
    const auto& k = j["kokoro"];
    c.kokoro.binary = require_string(k, "binary", c.kokoro.binary);
    c.kokoro.voice = require_string(k, "voice", c.kokoro.voice);
    c.kokoro.speed = require_number(k, "speed", c.kokoro.speed);
    c.kokoro.model = expand_home(require_string(k, "model", c.kokoro.model));
    c.kokoro.voices = expand_home(require_string(k, "voices", c.kokoro.voices));
  }

  if (c.device_token.empty() && c.devices.empty()) {
    throw std::runtime_error("config needs device_token or devices map");
  }
  if (c.arbiter_token.empty()) {
    throw std::runtime_error("config arbiter_token is required");
  }
  return c;
}

bool Config::authorize_device(const std::string& device_id,
                              const std::string& bearer_token) const {
  if (bearer_token.empty()) return false;
  auto it = devices.find(device_id);
  if (it != devices.end()) {
    return it->second == bearer_token;
  }
  if (!device_token.empty()) {
    return device_token == bearer_token;
  }
  return false;
}

}  // namespace intercom
