#pragma once

#include "intercom/turn_pipeline.hpp"

#include <memory>
#include <string>

namespace intercom {

class WhisperStt;
class PiperTts;

struct ServerDeps {
  Config config;
  std::shared_ptr<TurnPipeline> pipeline;
};

void run_http_server(ServerDeps deps);

}  // namespace intercom
