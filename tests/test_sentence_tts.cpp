#include "intercom/arbiter_client.hpp"
#include "intercom/session_store.hpp"
#include "intercom/stt.hpp"
#include "intercom/tts.hpp"
#include "intercom/turn_pipeline.hpp"
#include "intercom/warm.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_fails = 0;

void expect(bool cond, const char* expr, const char* file, int line) {
  if (!cond) {
    std::cerr << "FAIL " << file << ":" << line << " " << expr << "\n";
    ++g_fails;
  }
}

#define CHECK(cond) expect((cond), #cond, __FILE__, __LINE__)

class DummyStt : public intercom::SttProvider {
 public:
  std::string transcribe(const std::vector<std::uint8_t>&, int, int, std::string*) override {
    return {};
  }
  bool ready(std::string*) const override { return true; }
};

class RecordingTts : public intercom::TtsProvider {
 public:
  bool synthesize(const std::string& text, PcmChunkFn on_chunk, std::string*) override {
    {
      std::lock_guard<std::mutex> lk(mu);
      texts.push_back(text);
    }
    const std::uint8_t pcm[4] = {0, 0, 0, 0};
    if (on_chunk) on_chunk(pcm, sizeof(pcm));
    return true;
  }
  bool ready(std::string*) const override { return true; }

  std::size_t count() const {
    std::lock_guard<std::mutex> lk(mu);
    return texts.size();
  }
  std::vector<std::string> snapshot() const {
    std::lock_guard<std::mutex> lk(mu);
    return texts;
  }

 private:
  mutable std::mutex mu;
  std::vector<std::string> texts;
};

class StreamingArbiter : public intercom::ArbiterClient {
 public:
  explicit StreamingArbiter(std::shared_ptr<RecordingTts> tts)
      : ArbiterClient("http://127.0.0.1:9", "", "arthur"), tts_(std::move(tts)) {}

  std::optional<std::int64_t> create_conversation(const std::string&,
                                                  std::string*) const override {
    return 42;
  }

  bool send_message(std::int64_t, const std::string& message, const std::string&,
                    intercom::ArbiterStreamCallbacks cbs, std::atomic<bool>*,
                    std::string*) const override {
    last_message = message;
    if (cbs.on_request_id) cbs.on_request_id("req-test");
    if (cbs.on_text_delta) cbs.on_text_delta("Hello. ");
    for (int i = 0; i < 200 && tts_->count() < 1; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    spoke_first_before_done = tts_->count() >= 1;
    if (cbs.on_text_delta) cbs.on_text_delta("How are you?");
    if (cbs.on_done) cbs.on_done(true, "Hello. How are you?", "");
    return true;
  }

  bool cancel_request(const std::string&, std::string*) const override { return true; }

  mutable std::string last_message;
  mutable bool spoke_first_before_done = false;

 private:
  std::shared_ptr<RecordingTts> tts_;
};

class CollectingSink : public intercom::AudioSink {
 public:
  bool write(const std::uint8_t*, std::size_t) override { return true; }
};

}  // namespace

int main() {
  auto tts = std::make_shared<RecordingTts>();
  auto arbiter = std::make_shared<StreamingArbiter>(tts);
  auto stt = std::make_shared<DummyStt>();

  const auto db_path = std::filesystem::temp_directory_path() / "intercom_sentence_tts.db";
  std::filesystem::remove(db_path);
  auto sessions = std::make_shared<intercom::SessionStore>(db_path.string());
  std::string err;
  CHECK(sessions->open(&err));

  intercom::Config config;
  config.fast_path = false;
  config.filler.enabled = false;

  intercom::TurnPipeline pipeline(config, stt, tts, arbiter, sessions, nullptr);
  CollectingSink sink;
  const auto result = pipeline.run_text_utterance("dev-1", "tell me something long", sink);

  CHECK(result.ok);
  CHECK(result.error.empty());
  CHECK(arbiter->last_message == "tell me something long");
  CHECK(arbiter->spoke_first_before_done);

  const auto texts = tts->snapshot();
  CHECK(texts.size() == 2);
  if (texts.size() == 2) {
    CHECK(texts[0] == "Hello.");
    CHECK(texts[1] == "How are you?");
  }

  std::filesystem::remove(db_path);

  {
    auto tts_off = std::make_shared<RecordingTts>();
    auto arbiter_off = std::make_shared<StreamingArbiter>(tts_off);
    auto stt_off = std::make_shared<DummyStt>();
    const auto db_off = std::filesystem::temp_directory_path() / "intercom_warm_off.db";
    std::filesystem::remove(db_off);
    auto sessions_off = std::make_shared<intercom::SessionStore>(db_off.string());
    CHECK(sessions_off->open(&err));
    intercom::Config cfg_off;
    cfg_off.fast_path = false;
    cfg_off.filler.enabled = false;
    cfg_off.warm_prefix = false;
    cfg_off.devices["speaker-1"] = "tok";
    intercom::TurnPipeline pipe_off(cfg_off, stt_off, tts_off, arbiter_off, sessions_off,
                                    nullptr);
    pipe_off.warm_prefix();
    CHECK(arbiter_off->last_message.empty());
    std::filesystem::remove(db_off);
  }

  {
    auto tts_w = std::make_shared<RecordingTts>();
    auto arbiter_w = std::make_shared<StreamingArbiter>(tts_w);
    auto stt_w = std::make_shared<DummyStt>();
    const auto db_w = std::filesystem::temp_directory_path() / "intercom_warm.db";
    std::filesystem::remove(db_w);
    auto sessions_w = std::make_shared<intercom::SessionStore>(db_w.string());
    CHECK(sessions_w->open(&err));
    intercom::Config cfg_w;
    cfg_w.fast_path = false;
    cfg_w.filler.enabled = false;
    cfg_w.warm_prefix = true;
    cfg_w.devices["speaker-1"] = "tok";
    intercom::TurnPipeline pipe_w(cfg_w, stt_w, tts_w, arbiter_w, sessions_w, nullptr);
    pipe_w.warm_prefix();
    CHECK(arbiter_w->last_message == intercom::kPrefixWarmMessage);
    auto sess = sessions_w->get("speaker-1");
    CHECK(sess.has_value());
    if (sess) CHECK(sess->conversation_id == 42);
    std::filesystem::remove(db_w);
  }

  if (g_fails != 0) {
    std::cerr << g_fails << " failure(s)\n";
    return 1;
  }
  std::cout << "test_sentence_tts ok\n";
  return 0;
}
