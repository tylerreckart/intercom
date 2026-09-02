#pragma once

#include "intercom/http_server.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace intercom {

enum class WsOpcode : std::uint8_t {
  Cont = 0x0,
  Text = 0x1,
  Binary = 0x2,
  Close = 0x8,
  Ping = 0x9,
  Pong = 0xA,
};

struct WsFrame {
  bool fin = true;
  WsOpcode opcode = WsOpcode::Text;
  std::string payload;
};

std::string ws_accept_key(std::string_view client_key);
std::string encode_ws_frame(const WsFrame& frame, bool mask);
std::optional<WsFrame> decode_ws_frame(std::string_view data, std::size_t* consumed);

// Duplex voice socket: PCM up while PTT is held, PCM down on the same
// connection. HTTP PTT remains the fallback. Bind port 0 for an ephemeral
// test port.
class WsServer {
 public:
  explicit WsServer(ServerDeps deps);
  ~WsServer();

  WsServer(const WsServer&) = delete;
  WsServer& operator=(const WsServer&) = delete;

  bool listen(const std::string& host, int port);
  void stop();
  int port() const { return port_; }

 private:
  void accept_loop();
  void handle_client(int fd);

  ServerDeps deps_;
  int listen_fd_ = -1;
  int port_ = 0;
  std::atomic<bool> stop_{false};
  std::thread accept_thread_;
};

}  // namespace intercom
