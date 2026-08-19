#include "alfred/turn_pipeline.hpp"
#include "alfred/speakable.hpp"
#include "alfred/util.hpp"

#include <chrono>

namespace alfred {
namespace {

std::int64_t now_unix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

TurnPipeline::TurnPipeline(Config config,
                           std::shared_ptr<SttProvider> stt,
                           std::shared_ptr<TtsProvider> tts,
                           std::shared_ptr<ArbiterClient> arbiter,
                           std::shared_ptr<SessionStore> sessions)
    : config_(std::move(config)),
      stt_(std::move(stt)),
      tts_(std::move(tts)),
      arbiter_(std::move(arbiter)),
      sessions_(std::move(sessions)),
      fast_path_(config_.fast_path) {}

void TurnPipeline::register_turn(std::shared_ptr<TurnHandle> h) {
  std::lock_guard<std::mutex> lk(turns_mu_);
  turns_[h->turn_id] = std::move(h);
}

void TurnPipeline::unregister_turn(const std::string& turn_id) {
  std::lock_guard<std::mutex> lk(turns_mu_);
  turns_.erase(turn_id);
}

std::shared_ptr<TurnHandle> TurnPipeline::find_turn(const std::string& turn_id) {
  std::lock_guard<std::mutex> lk(turns_mu_);
  auto it = turns_.find(turn_id);
  if (it == turns_.end()) return nullptr;
  return it->second;
}

std::optional<DeviceSession> TurnPipeline::session_for(const std::string& device_id) const {
  return sessions_->get(device_id);
}

std::int64_t TurnPipeline::ensure_conversation(const std::string& device_id, std::string* err) {
  if (auto existing = sessions_->get(device_id)) {
    if (existing->conversation_id > 0) {
      return existing->conversation_id;
    }
  }
  auto id = arbiter_->create_conversation("alfred:" + device_id, err);
  if (!id || *id <= 0) {
    if (err && err->empty()) *err = "conversation create failed";
    return 0;
  }
  DeviceSession s;
  s.device_id = device_id;
  s.conversation_id = *id;
  s.updated_at = now_unix();
  if (!sessions_->upsert(s, err)) return 0;
  return *id;
}

bool TurnPipeline::cancel_turn(const std::string& turn_id, std::string* err) {
  auto h = find_turn(turn_id);
  if (!h) {
    if (err) *err = "turn not found";
    return false;
  }
  h->cancel.store(true);
  std::string rid;
  {
    std::lock_guard<std::mutex> lk(h->mu);
    rid = h->arbiter_request_id;
  }
  if (!rid.empty()) {
    return arbiter_->cancel_request(rid, err);
  }
  return true;
}

TurnResult TurnPipeline::run_utterance(const std::string& device_id,
                                       const std::vector<std::uint8_t>& pcm,
                                       int sample_rate,
                                       int channels,
                                       AudioSink& sink) {
  std::string err;
  const std::string transcript = stt_->transcribe(pcm, sample_rate, channels, &err);
  if (transcript.empty()) {
    TurnResult result;
    result.turn_id = make_turn_id();
    result.error = err.empty() ? "stt failed" : err;
    return result;
  }
  return run_text_utterance(device_id, transcript, sink);
}

TurnResult TurnPipeline::run_text_utterance(const std::string& device_id,
                                            const std::string& transcript,
                                            AudioSink& sink,
                                            std::string turn_id) {
  TurnResult result;
  result.turn_id = turn_id.empty() ? make_turn_id() : std::move(turn_id);
  result.transcript = transcript;
  auto handle = std::make_shared<TurnHandle>();
  handle->turn_id = result.turn_id;
  register_turn(handle);

  auto finish = [&](TurnResult r) {
    unregister_turn(result.turn_id);
    return r;
  };

  if (handle->cancel.load()) {
    result.error = "canceled";
    return finish(result);
  }

  std::string err;
  int spoken_chunks = 0;
  auto speak = [&](const std::string& text) -> bool {
    const std::string spoken = to_speakable(text);
    if (spoken.empty()) return true;
    return tts_->synthesize(
        spoken,
        [&](const std::uint8_t* data, std::size_t len) {
          if (handle->cancel.load()) return false;
          ++spoken_chunks;
          return sink.write(data, len);
        },
        &err);
  };

  if (auto fp = fast_path_.try_handle(result.transcript)) {
    result.used_fast_path = true;
    if (auto existing = sessions_->get(device_id)) {
      result.conversation_id = existing->conversation_id;
    }
    if (!speak(fp->reply)) {
      result.error = err.empty() ? "tts failed" : err;
      return finish(result);
    }
    result.ok = true;
    if (result.conversation_id > 0) {
      DeviceSession s;
      s.device_id = device_id;
      s.conversation_id = result.conversation_id;
      s.last_turn_id = result.turn_id;
      s.updated_at = now_unix();
      sessions_->upsert(s, &err);
    }
    return finish(result);
  }

  const std::int64_t conv = ensure_conversation(device_id, &err);
  if (conv == 0) {
    result.error = err.empty() ? "conversation create failed" : err;
    return finish(result);
  }
  result.conversation_id = conv;

  std::string speak_buf;
  bool done_ok = false;
  std::string done_err;
  std::string full_content;

  ArbiterStreamCallbacks cbs;
  cbs.on_request_id = [&](const std::string& rid) {
    std::lock_guard<std::mutex> lk(handle->mu);
    handle->arbiter_request_id = rid;
  };
  cbs.on_text_delta = [&](const std::string& delta) {
    if (handle->cancel.load()) return;
    speak_buf += delta;
  };
  cbs.on_done = [&](bool ok, const std::string& content, const std::string& error) {
    done_ok = ok;
    full_content = content;
    done_err = error;
  };

  const bool streamed = arbiter_->send_message(
      conv, voice_user_message(result.transcript), result.turn_id, cbs,
      &handle->cancel, &err);

  if (handle->cancel.load()) {
    result.error = "canceled";
    return finish(result);
  }

  std::string spoken_text;
  for (const auto& sentence : flush_sentences(speak_buf, true)) {
    if (!spoken_text.empty()) spoken_text.push_back(' ');
    spoken_text += sentence;
  }
  if (spoken_text.empty()) spoken_text = full_content;
  if (!speak(spoken_text)) {
    result.error = err.empty() ? "tts failed" : err;
    return finish(result);
  }

  DeviceSession s;
  s.device_id = device_id;
  s.conversation_id = conv;
  s.last_turn_id = result.turn_id;
  s.updated_at = now_unix();
  sessions_->upsert(s, &err);

  if (!streamed) {
    result.error = err.empty() ? "arbiter stream failed" : err;
    return finish(result);
  }

  result.ok = done_ok;
  if (!done_ok) {
    result.error = done_err.empty() ? "arbiter done ok=false" : done_err;
  }
  return finish(result);
}

}  // namespace alfred
