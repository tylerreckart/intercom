#include "alfred/http_server.hpp"
#include "alfred/util.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace alfred {
namespace {

std::string bearer_token(const httplib::Request& req) {
  const auto auth = req.get_header_value("Authorization");
  constexpr std::string_view prefix = "Bearer ";
  if (auth.size() > prefix.size() &&
      auth.compare(0, prefix.size(), prefix) == 0) {
    return auth.substr(prefix.size());
  }
  return {};
}

int parse_sample_rate(const httplib::Request& req, int fallback) {
  if (req.has_header("X-Sample-Rate")) {
    try {
      return std::stoi(req.get_header_value("X-Sample-Rate"));
    } catch (...) {
    }
  }
  const auto ct = req.get_header_value("Content-Type");
  const auto pos = ct.find("rate=");
  if (pos != std::string::npos) {
    try {
      return std::stoi(ct.substr(pos + 5));
    } catch (...) {
    }
  }
  return fallback;
}

class CollectingSink : public AudioSink {
 public:
  std::vector<std::uint8_t> pcm;

  bool write(const std::uint8_t* data, std::size_t len) override {
    pcm.insert(pcm.end(), data, data + len);
    return true;
  }
};

std::string header_safe(std::string s) {
  for (char& c : s) {
    if (c == '\r' || c == '\n' || c == '\0') c = ' ';
  }
  if (s.size() > 240) s.resize(240);
  return s;
}

void send_turn_response(httplib::Response& res, const TurnResult& result,
                        std::vector<std::uint8_t>&& pcm, int sample_rate,
                        const std::string& device_id) {
  res.set_header("X-Turn-Id", result.turn_id);
  res.set_header("X-Transcript", result.transcript);
  res.set_header("X-Device-Id", device_id);
  if (result.conversation_id != 0) {
    res.set_header("X-Conversation-Id", std::to_string(result.conversation_id));
  }
  if (result.used_fast_path) {
    res.set_header("X-Fast-Path", "1");
  }

  const bool failed = !result.ok || !result.error.empty() || pcm.empty();
  if (failed && pcm.empty()) {
    const std::string err = result.error.empty() ? "empty tts audio" : result.error;
    std::cerr << "alfred turn error: " << err << std::endl;
    res.status = 502;
    res.set_header("X-Alfred-Error", header_safe(err));
    nlohmann::json j = {
        {"error", err},
        {"ok", false},
        {"turn_id", result.turn_id},
        {"fast_path", result.used_fast_path},
    };
    res.set_content(j.dump(), "application/json");
    return;
  }

  if (!result.error.empty()) {
    std::cerr << "alfred turn warning: " << result.error << std::endl;
    res.set_header("X-Alfred-Error", header_safe(result.error));
  }

  const std::string ctype =
      "audio/L16; rate=" + std::to_string(sample_rate) + "; channels=1";
  res.status = 200;
  res.set_content(std::string(pcm.begin(), pcm.end()), ctype);
}

}  // namespace

void run_http_server(ServerDeps deps) {
  httplib::Server svr;

  svr.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
    std::string stt_detail;
    std::string tts_detail;
    std::string arb_detail;
    const bool stt_ok = deps.pipeline->stt().ready(&stt_detail);
    const bool tts_ok = deps.pipeline->tts().ready(&tts_detail);
    const bool arb_ok = deps.pipeline->arbiter().health_reachable(&arb_detail);
    nlohmann::json j = {
        {"ok", stt_ok && tts_ok},
        {"whisper", {{"ready", stt_ok}, {"detail", stt_detail}}},
        {"piper", {{"ready", tts_ok}, {"detail", tts_detail}}},
        {"arbiter", {{"reachable", arb_ok}, {"detail", arb_detail}}},
    };
    res.status = (stt_ok && tts_ok) ? 200 : 503;
    res.set_content(j.dump(2), "application/json");
  });

  svr.Get(R"(/v1/devices/([^/]+)/session)",
          [&](const httplib::Request& req, httplib::Response& res) {
            const std::string device_id = req.matches[1];
            const std::string token = bearer_token(req);
            if (!deps.config.authorize_device(device_id, token)) {
              res.status = 401;
              res.set_content(R"({"error":"unauthorized"})", "application/json");
              return;
            }
            auto session = deps.pipeline->session_for(device_id);
            if (!session) {
              res.status = 404;
              res.set_content(R"({"error":"no session"})", "application/json");
              return;
            }
            nlohmann::json j = {
                {"device_id", session->device_id},
                {"conversation_id", session->conversation_id},
                {"last_turn_id", session->last_turn_id},
                {"updated_at", session->updated_at},
            };
            res.set_content(j.dump(2), "application/json");
          });

  svr.Post(R"(/v1/turns/([^/]+)/cancel)",
           [&](const httplib::Request& req, httplib::Response& res) {
             const std::string turn_id = req.matches[1];
             const std::string device_id = req.get_header_value("X-Device-Id");
             const std::string token = bearer_token(req);
             if (device_id.empty() ||
                 !deps.config.authorize_device(device_id, token)) {
               res.status = 401;
               res.set_content(R"({"error":"unauthorized"})", "application/json");
               return;
             }
             std::string err;
             if (!deps.pipeline->cancel_turn(turn_id, &err)) {
               res.status = 404;
               nlohmann::json j = {{"error", err.empty() ? "cancel failed" : err}};
               res.set_content(j.dump(), "application/json");
               return;
             }
             res.set_content(R"({"cancelled":true})", "application/json");
           });

  svr.Post("/v1/utterance", [&](const httplib::Request& req, httplib::Response& res) {
    const std::string device_id = req.get_header_value("X-Device-Id");
    const std::string token = bearer_token(req);
    if (device_id.empty() || !deps.config.authorize_device(device_id, token)) {
      res.status = 401;
      res.set_content(R"({"error":"unauthorized"})", "application/json");
      return;
    }
    if (req.body.empty()) {
      res.status = 400;
      res.set_content(R"({"error":"empty body"})", "application/json");
      return;
    }

    const int sample_rate = parse_sample_rate(req, deps.config.sample_rate);
    std::vector<std::uint8_t> pcm(req.body.begin(), req.body.end());

    std::string stt_err;
    const std::string transcript = deps.pipeline->stt().transcribe(
        pcm, sample_rate, deps.config.channels, &stt_err);
    if (transcript.empty()) {
      res.status = 502;
      const std::string err = stt_err.empty() ? "stt failed" : stt_err;
      res.set_header("X-Alfred-Error", header_safe(err));
      nlohmann::json j = {{"error", err}};
      res.set_content(j.dump(), "application/json");
      return;
    }

    const std::string turn_id = alfred::make_turn_id();
    CollectingSink audio;
    const TurnResult result = deps.pipeline->run_text_utterance(
        device_id, transcript, audio, turn_id);
    send_turn_response(res, result, std::move(audio.pcm), deps.config.sample_rate,
                       device_id);
  });

  // Optional JSON text utterance for bridge testing without PCM.
  svr.Post("/v1/utterance/text", [&](const httplib::Request& req, httplib::Response& res) {
    const std::string device_id = req.get_header_value("X-Device-Id");
    const std::string token = bearer_token(req);
    if (device_id.empty() || !deps.config.authorize_device(device_id, token)) {
      res.status = 401;
      res.set_content(R"({"error":"unauthorized"})", "application/json");
      return;
    }
    std::string transcript;
    try {
      auto j = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
      if (j.contains("text") && j["text"].is_string()) {
        transcript = j["text"].get<std::string>();
      }
    } catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"invalid json"})", "application/json");
      return;
    }
    if (transcript.empty()) {
      res.status = 400;
      res.set_content(R"({"error":"missing text"})", "application/json");
      return;
    }

    const std::string turn_id = alfred::make_turn_id();
    CollectingSink audio;
    const TurnResult result = deps.pipeline->run_text_utterance(
        device_id, transcript, audio, turn_id);
    send_turn_response(res, result, std::move(audio.pcm), deps.config.sample_rate,
                       device_id);
  });

  std::cout << "alfred listening on http://" << deps.config.listen_host << ":"
            << deps.config.listen_port << std::endl;
  if (!svr.listen(deps.config.listen_host.c_str(), deps.config.listen_port)) {
    std::cerr << "alfred failed to bind " << deps.config.listen_host << ":"
              << deps.config.listen_port << std::endl;
  }
}

}  // namespace alfred
