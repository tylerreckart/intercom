#include "alfred/config.hpp"
#include "alfred/util.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace alfred {
namespace {

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

bool require_bool(const nlohmann::json& j, const char* key, bool def) {
  if (!j.contains(key) || j[key].is_null()) return def;
  if (!j[key].is_boolean()) {
    throw std::runtime_error(std::string("config field '") + key + "' must be a boolean");
  }
  return j[key].get<bool>();
}

}  // namespace

Config Config::load(const std::string& path) {
  std::ifstream in(path);
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
  c.session_db = expand_home(require_string(j, "session_db", c.session_db));
  c.sample_rate = require_int(j, "sample_rate", c.sample_rate);
  c.channels = require_int(j, "channels", c.channels);
  c.fast_path = require_bool(j, "fast_path", c.fast_path);

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

}  // namespace alfred
