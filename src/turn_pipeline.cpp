#include "intercom/turn_pipeline.hpp"
#include "intercom/speakable.hpp"
#include "intercom/util.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <optional>
#include <thread>

namespace intercom {
namespace {

std::int64_t now_unix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void sleep_ms(int ms) {
  if (ms <= 0) return;
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

struct ArbiterRunState {
  std::atomic<bool> got_first_delta{false};
  std::atomic<bool> done{false};
  bool streamed = false;
  bool done_ok = false;
  std::string done_err;
  std::string full_content;
  std::string stream_err;
  std::string speak_buf;
  std::mutex speak_mu;
};

}  // namespace

TurnPipeline::TurnPipeline(Config config,
                           std::shared_ptr<SttProvider> stt,
                           std::shared_ptr<TtsProvider> tts,
                           std::shared_ptr<ArbiterClient> arbiter,
                           std::shared_ptr<SessionStore> sessions,
                           std::shared_ptr<FillerClient> filler)
    : config_(std::move(config)),
      stt_(std::move(stt)),
      tts_(std::move(tts)),
      arbiter_(std::move(arbiter)),
      sessions_(std::move(sessions)),
      filler_(std::move(filler)),
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
  auto id = arbiter_->create_conversation("intercom:" + device_id, err);
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
  auto speak = [&](const std::string& text) -> bool {
    const std::string spoken = to_speakable(text);
    if (spoken.empty()) return true;
    return tts_->synthesize(
        spoken,
        [&](const std::uint8_t* data, std::size_t len) {
          if (handle->cancel.load()) return false;
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

  const bool filler_enabled =
      filler_ && filler_->enabled() && config_.filler.enabled;
  const auto turn_start = std::chrono::steady_clock::now();

  std::future<std::string> initial_filler_future;
  if (filler_enabled) {
    initial_filler_future = std::async(std::launch::async, [&, transcript]() {
      std::string filler_err;
      const std::string phrase = filler_->generate(
          transcript, FillerStage::Initial, "", &handle->cancel, &filler_err);
      if (!filler_err.empty()) {
        std::cerr << "intercom filler: " << filler_err << std::endl;
      } else if (!phrase.empty()) {
        std::cerr << "intercom filler: generated \"" << phrase << "\"" << std::endl;
      } else if (!handle->cancel.load()) {
        std::cerr << "intercom filler: empty phrase" << std::endl;
      }
      return phrase;
    });
  }

  std::string conv_err;
  auto conv_future = std::async(std::launch::async, [&]() {
    return ensure_conversation(device_id, &conv_err);
  });

  const std::int64_t conv = conv_future.get();
  if (conv == 0) {
    result.error = conv_err.empty() ? "conversation create failed" : conv_err;
    return finish(result);
  }
  result.conversation_id = conv;

  ArbiterRunState arb_state;
  std::string spoken_filler;
  bool instant_spoken = false;
  bool contextual_spoken = false;
  int followups_spoken = 0;
  std::optional<std::chrono::steady_clock::time_point> next_followup_at;
  std::future<std::string> followup_future;

  ArbiterStreamCallbacks cbs;
  cbs.on_request_id = [&](const std::string& rid) {
    std::lock_guard<std::mutex> lk(handle->mu);
    handle->arbiter_request_id = rid;
  };
  cbs.on_text_delta = [&](const std::string& delta) {
    if (handle->cancel.load()) return;
    arb_state.got_first_delta.store(true);
    std::lock_guard<std::mutex> lk(arb_state.speak_mu);
    arb_state.speak_buf += delta;
  };
  cbs.on_done = [&](bool ok, const std::string& content, const std::string& error) {
    arb_state.done_ok = ok;
    arb_state.full_content = content;
    arb_state.done_err = error;
    arb_state.done.store(true);
  };

  auto arbiter_future = std::async(std::launch::async, [&]() {
    std::string stream_err;
    const bool streamed = arbiter_->send_message(
        conv, voice_user_message(result.transcript), result.turn_id, cbs,
        &handle->cancel, &stream_err);
    arb_state.streamed = streamed;
    arb_state.stream_err = std::move(stream_err);
    arb_state.done.store(true);
    return streamed;
  });

  auto maybe_speak_filler = [&](const std::string& phrase, bool is_instant = false) -> bool {
    if (phrase.empty() || handle->cancel.load() || arb_state.done.load()) {
      return true;
    }
    if (!is_instant && phrase == spoken_filler) return true;
    std::cerr << "intercom filler: speaking \"" << phrase << "\"" << std::endl;
    spoken_filler = phrase;
    if (!is_instant && config_.filler.max_followups > 0) {
      next_followup_at = std::chrono::steady_clock::now() +
                         std::chrono::milliseconds(config_.filler.followup_silence_ms);
    }
    return speak(phrase);
  };

  while (!arb_state.done.load()) {
    if (handle->cancel.load()) break;

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - turn_start)
                                .count();

    if (filler_enabled && config_.filler.instant_ack_ms > 0 && !instant_spoken &&
        spoken_filler.empty() && elapsed_ms >= config_.filler.instant_ack_ms) {
      instant_spoken = true;
      if (!maybe_speak_filler(FillerClient::instant_ack(), true)) {
        result.error = err.empty() ? "tts failed" : err;
        handle->cancel.store(true);
        break;
      }
    }

    if (filler_enabled && !contextual_spoken && initial_filler_future.valid()) {
      const bool past_min_silence = elapsed_ms >= config_.filler.min_silence_ms;
      const bool ready_after_instant = instant_spoken || past_min_silence;
      if (ready_after_instant &&
          initial_filler_future.wait_for(std::chrono::milliseconds(0)) ==
              std::future_status::ready) {
        contextual_spoken = true;
        if (!maybe_speak_filler(initial_filler_future.get())) {
          result.error = err.empty() ? "tts failed" : err;
          handle->cancel.store(true);
          break;
        }
      }
    }

    if (filler_enabled && next_followup_at && followups_spoken < config_.filler.max_followups &&
        std::chrono::steady_clock::now() >= *next_followup_at) {
      next_followup_at.reset();
      if (!followup_future.valid()) {
        followup_future = std::async(std::launch::async, [&, transcript]() {
          std::string filler_err;
          return filler_->generate(transcript, FillerStage::FollowUp, spoken_filler,
                                  &handle->cancel, &filler_err);
        });
      }
    }

    if (followup_future.valid() &&
        followup_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      ++followups_spoken;
      const std::string followup = followup_future.get();
      followup_future = {};
      if (!followup.empty() && followup != spoken_filler) {
        if (!maybe_speak_filler(followup)) {
          result.error = err.empty() ? "tts failed" : err;
          handle->cancel.store(true);
          break;
        }
      }
    }

    sleep_ms(20);
  }

  arbiter_future.get();

  if (handle->cancel.load()) {
    result.error = "canceled";
    return finish(result);
  }

  std::string speak_buf;
  {
    std::lock_guard<std::mutex> lk(arb_state.speak_mu);
    speak_buf = std::move(arb_state.speak_buf);
  }

  std::string spoken_text;
  for (const auto& sentence : flush_sentences(speak_buf, true)) {
    if (!spoken_text.empty()) spoken_text.push_back(' ');
    spoken_text += sentence;
  }
  if (spoken_text.empty()) spoken_text = arb_state.full_content;
  if (!spoken_text.empty() && !speak(spoken_text)) {
    result.error = err.empty() ? "tts failed" : err;
    return finish(result);
  }

  DeviceSession s;
  s.device_id = device_id;
  s.conversation_id = conv;
  s.last_turn_id = result.turn_id;
  s.updated_at = now_unix();
  sessions_->upsert(s, &err);

  if (!arb_state.streamed) {
    result.error =
        arb_state.stream_err.empty() ? "arbiter stream failed" : arb_state.stream_err;
    return finish(result);
  }

  result.ok = arb_state.done_ok;
  if (!arb_state.done_ok) {
    result.error = arb_state.done_err.empty() ? "arbiter done ok=false" : arb_state.done_err;
  }
  return finish(result);
}

}  // namespace intercom
