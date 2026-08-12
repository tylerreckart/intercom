#include "alfred/arbiter_client.hpp"
#include "alfred/config.hpp"
#include "alfred/http_server.hpp"
#include "alfred/session_store.hpp"
#include "alfred/stt_whisper.hpp"
#include "alfred/tts_piper.hpp"
#include "alfred/turn_pipeline.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace {

void usage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " --config <path>\n"
            << "  Alfred — local voice bridge for Arbiter (whisper.cpp + Piper).\n";
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

  alfred::Config config;
  try {
    config = alfred::Config::load(config_path);
  } catch (const std::exception& e) {
    std::cerr << "config error: " << e.what() << "\n";
    return 1;
  }

  auto sessions = std::make_shared<alfred::SessionStore>(config.session_db);
  std::string err;
  if (!sessions->open(&err)) {
    std::cerr << "session store: " << err << "\n";
    return 1;
  }

  auto stt = std::make_shared<alfred::WhisperStt>(config.whisper);
  auto tts = std::make_shared<alfred::PiperTts>(config.piper, config.sample_rate);
  auto arbiter = std::make_shared<alfred::ArbiterClient>(
      config.arbiter_base_url, config.arbiter_token, config.agent);
  auto pipeline = std::make_shared<alfred::TurnPipeline>(
      config, stt, tts, arbiter, sessions);

  alfred::ServerDeps deps;
  deps.config = config;
  deps.pipeline = pipeline;
  alfred::run_http_server(std::move(deps));
  return 0;
}
