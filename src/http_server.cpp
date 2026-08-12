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

class ChunkedPcmSink : public AudioSink {
 public:
  explicit ChunkedPcmSink(httplib::DataSink& sink) : sink_(sink) {}

  bool write(const std::uint8_t* data, std::size_t len) override {
    return sink_.write(reinterpret_cast<const char*>(data), len);
  }

 private:
  httplib::DataSink& sink_;
};

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
      nlohmann::json j = {{"error", stt_err.empty() ? "stt failed" : stt_err}};
      res.set_content(j.dump(), "application/json");
      return;
    }

    // Pre-assign turn id by running text path inside provider; set known headers now.
    // Conversation may be created during the turn — expose prior session if any.
    if (auto session = deps.pipeline->session_for(device_id)) {
      res.set_header("X-Conversation-Id", std::to_string(session->conversation_id));
    }

    const std::string turn_id = alfred::make_turn_id();
    const std::string ctype =
        "audio/L16; rate=" + std::to_string(deps.config.sample_rate) +
        "; channels=1";
    res.set_header("Content-Type", ctype);
    res.set_header("X-Transcript", transcript);
    res.set_header("X-Device-Id", device_id);
    res.set_header("X-Turn-Id", turn_id);

    struct State {
      bool ran = false;
      std::string device_id;
      std::string transcript;
      std::string turn_id;
      TurnResult result;
    };
    auto state = std::make_shared<State>();
    state->device_id = device_id;
    state->transcript = transcript;
    state->turn_id = turn_id;

    res.set_content_provider(
        ctype, [deps, state](size_t, httplib::DataSink& sink) {
          if (state->ran) {
            sink.done();
            return true;
          }
          state->ran = true;
          ChunkedPcmSink audio(sink);
          state->result = deps.pipeline->run_text_utterance(
              state->device_id, state->transcript, audio, state->turn_id);
          sink.done();
          return true;
        });
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

    if (auto session = deps.pipeline->session_for(device_id)) {
      res.set_header("X-Conversation-Id", std::to_string(session->conversation_id));
    }
    const std::string turn_id = alfred::make_turn_id();
    const std::string ctype =
        "audio/L16; rate=" + std::to_string(deps.config.sample_rate) +
        "; channels=1";
    res.set_header("Content-Type", ctype);
    res.set_header("X-Transcript", transcript);
    res.set_header("X-Device-Id", device_id);
    res.set_header("X-Turn-Id", turn_id);

    struct State {
      bool ran = false;
      std::string device_id;
      std::string transcript;
      std::string turn_id;
    };
    auto state = std::make_shared<State>();
    state->device_id = device_id;
    state->transcript = transcript;
    state->turn_id = turn_id;

    res.set_content_provider(
        ctype, [deps, state](size_t, httplib::DataSink& sink) {
          if (state->ran) {
            sink.done();
            return true;
          }
          state->ran = true;
          ChunkedPcmSink audio(sink);
          auto result = deps.pipeline->run_text_utterance(
              state->device_id, state->transcript, audio, state->turn_id);
          (void)result;
          sink.done();
          return true;
        });
  });

  std::cout << "alfred listening on http://" << deps.config.listen_host << ":"
            << deps.config.listen_port << std::endl;
  if (!svr.listen(deps.config.listen_host.c_str(), deps.config.listen_port)) {
    std::cerr << "alfred failed to bind " << deps.config.listen_host << ":"
              << deps.config.listen_port << std::endl;
  }
}

}  // namespace alfred
