#include "intercom/arbiter_client.hpp"
#include "intercom/config.hpp"
#include "intercom/http_server.hpp"
#include "intercom/session_store.hpp"
#include "intercom/stt_whisper.hpp"
#include "intercom/tts_piper.hpp"
#include "intercom/tts_kokoro.hpp"
#include "intercom/turn_pipeline.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace {

void usage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " --config <path>\n"
            << "  Intercom — local voice bridge for Arbiter (whisper.cpp + TTS).\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string config_path;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    } else if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      return 0;
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      usage(argv[0]);
      return 2;
    }
  }
  if (config_path.empty()) {
    usage(argv[0]);
    return 2;
  }

  intercom::Config config;
  try {
    config = intercom::Config::load(config_path);
  } catch (const std::exception& e) {
    std::cerr << "config error: " << e.what() << "\n";
    return 1;
  }

  auto sessions = std::make_shared<intercom::SessionStore>(config.session_db);
  std::string err;
  if (!sessions->open(&err)) {
    std::cerr << "session store: " << err << "\n";
    return 1;
  }

  auto stt = std::make_shared<intercom::WhisperStt>(config.whisper);
  std::shared_ptr<intercom::TtsProvider> tts;
  if (config.tts_provider == "kokoro") {
    tts = std::make_shared<intercom::KokoroTts>(config.kokoro, config.sample_rate);
  } else {
    tts = std::make_shared<intercom::PiperTts>(config.piper, config.sample_rate);
  }
  auto arbiter = std::make_shared<intercom::ArbiterClient>(
      config.arbiter_base_url, config.arbiter_token, config.agent,
      config.agent_def_json);
  auto pipeline = std::make_shared<intercom::TurnPipeline>(
      config, stt, tts, arbiter, sessions);

  intercom::ServerDeps deps;
  deps.config = config;
  deps.pipeline = pipeline;
  intercom::run_http_server(std::move(deps));
  return 0;
}
