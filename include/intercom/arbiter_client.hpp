#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <string>

namespace intercom {

struct ArbiterStreamCallbacks {
  // Fires once when request_received yields a request_id.
  std::function<void(const std::string& request_id)> on_request_id;
  // Master (depth 0) text deltas only.
  std::function<void(const std::string& delta)> on_text_delta;
  // Master (depth 0) tool_call — name only; do not speak it.
  std::function<void(const std::string& tool)> on_tool_call;
  // Terminal done content (full reply) and ok flag.
  std::function<void(bool ok, const std::string& content, const std::string& error)> on_done;
};

class ArbiterClient {
 public:
  ArbiterClient(std::string base_url, std::string token, std::string agent,
                std::string agent_def_json = "");
  virtual ~ArbiterClient() = default;

  // Create a conversation; returns id or nullopt.
  virtual std::optional<std::int64_t> create_conversation(const std::string& title,
                                                          std::string* err) const;

  // Stream a user message. idempotency_key maps to Idempotency-Key header.
  // cancel_flag: when set, client stops reading and returns early.
  virtual bool send_message(std::int64_t conversation_id,
                            const std::string& message,
                            const std::string& idempotency_key,
                            ArbiterStreamCallbacks cbs,
                            std::atomic<bool>* cancel_flag,
                            std::string* err) const;

  virtual bool cancel_request(const std::string& request_id, std::string* err) const;

  bool health_reachable(std::string* detail) const;

 private:
  std::string base_url_;
  std::string token_;
  std::string agent_;
  std::string agent_def_json_;
};

}  // namespace intercom
