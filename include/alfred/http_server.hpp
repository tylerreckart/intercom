#pragma once

#include "alfred/turn_pipeline.hpp"

#include <memory>
#include <string>

namespace alfred {

class WhisperStt;
class PiperTts;

struct ServerDeps {
  Config config;
  std::shared_ptr<TurnPipeline> pipeline;
};

void run_http_server(ServerDeps deps);

}  // namespace alfred
