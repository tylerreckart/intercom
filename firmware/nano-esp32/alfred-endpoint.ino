#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <math.h>

#include "driver/i2s.h"
#include "driver/gpio.h"
#include "config.h"

enum class PlayResult { Done, BargeIn, Error };

struct HttpHeaders {
  int status = 0;
  bool chunked = false;
  int contentLength = -1;
  String turnId;
  String transcript;
  String alfredError;
  bool fastPath = false;
};

// Thin PCM endpoint for Alfred (docs/device.md):
// hold PTT -> record 16 kHz s16le -> POST /v1/utterance -> play chunked PCM.
// Serial (idle): tone | health | ping

#define I2S_PORT I2S_NUM_0

static gpio_num_t i2sGpio(int pin) {
  return (gpio_num_t)digitalPinToGPIONumber(pin);
}

static void i2sStop() { i2s_driver_uninstall(I2S_PORT); }

static String gDeviceId;
static int16_t *gPcm = nullptr;
static size_t gMaxSamples = 0;
static String gTurnId;

static bool gThinking = false;
static bool gThinkLit = false;
static uint32_t gThinkLastMs = 0;
static int gVolQ15 = 32767;
static uint32_t gVolLastMs = 0;
static bool gPttArmed = false;
static size_t gPlayBytes = 0;

static gpio_num_t ampGpio() {
  return (gpio_num_t)digitalPinToGPIONumber(PIN_AMP_SD);
}

static void ampInit() {
  const gpio_num_t g = ampGpio();
  gpio_reset_pin(g);
  gpio_set_direction(g, GPIO_MODE_OUTPUT);
  gpio_set_level(g, 0);
}

static void ampMute(bool mute) { gpio_set_level(ampGpio(), mute ? 0 : 1); }

static void thinkLed(bool on) {
  gThinkLit = on;
  digitalWrite(PIN_THINK_LED, on ? HIGH : LOW);
}

static void updatePttLed() {
  if (gThinking) return;
  thinkLed(pttHeld());
}

static void thinking(bool active) {
  gThinking = active;
  if (!active) thinkLed(false);
}

static void thinkingTick() {
  if (!gThinking) return;
  const uint32_t now = millis();
  if (now - gThinkLastMs < THINK_BLINK_MS) return;
  gThinkLastMs = now;
  thinkLed(!gThinkLit);
}

struct Thinking {
  Thinking() { thinking(true); }
  ~Thinking() { thinking(false); }
};

static void updateVolume() {
  const uint32_t now = millis();
  if (now - gVolLastMs < VOL_UPDATE_MS) return;
  gVolLastMs = now;
  const int raw = analogRead(PIN_VOLUME);
  const uint32_t n = (uint32_t)constrain(raw, 0, ADC_MAX);
  // A-taper is already audio/log; map ADC linearly to gain.
  gVolQ15 = (int)((n * 32767ULL) / (uint32_t)ADC_MAX);
}

static int16_t scaleSample(int16_t s) {
  return (int16_t)(((int32_t)s * gVolQ15) >> 15);
}

static void dieBlink() {
  for (;;) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(80);
    digitalWrite(LED_BUILTIN, LOW);
    delay(80);
  }
}

static bool allocCapture() {
  gMaxSamples = (size_t)SAMPLE_RATE * (MAX_RECORD_MS / 1000);
  const size_t bytes = gMaxSamples * sizeof(int16_t);
  gPcm = (int16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!gPcm) {
    gPcm = (int16_t *)malloc(bytes);
  }
  return gPcm != nullptr;
}

static bool startMic() {
  i2sStop();
  const i2s_config_t cfg = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0,
      .mclk_multiple = I2S_MCLK_MULTIPLE_256,
      .bits_per_chan = I2S_BITS_PER_CHAN_32BIT,
      .chan_mask = I2S_TDM_ACTIVE_CH0,
      .total_chan = 2,
      .left_align = false,
      .big_edin = false,
      .bit_order_msb = false,
      .skip_msk = false,
  };
  esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);
  if (err != ESP_OK) {
    Serial.printf("mic i2s install err %d\n", (int)err);
    return false;
  }
  const i2s_pin_config_t pins = {.mck_io_num = I2S_PIN_NO_CHANGE,
                                 .bck_io_num = i2sGpio(PIN_MIC_BCLK),
                                 .ws_io_num = i2sGpio(PIN_MIC_WS),
                                 .data_out_num = I2S_PIN_NO_CHANGE,
                                 .data_in_num = i2sGpio(PIN_MIC_SD)};
  err = i2s_set_pin(I2S_PORT, &pins);
  if (err != ESP_OK) {
    Serial.printf("mic i2s pin err %d\n", (int)err);
    i2sStop();
    return false;
  }
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
  i2s_zero_dma_buffer(I2S_PORT);
  i2s_start(I2S_PORT);
  return true;
}

static void stopMic() { i2sStop(); }

static uint32_t countGpioFlips(gpio_num_t pin, uint32_t ms, uint32_t *highsOut) {
  uint32_t highs = 0, flips = 0;
  int last = gpio_get_level(pin);
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    const int v = gpio_get_level(pin);
    if (v) highs++;
    if (v != last) {
      flips++;
      last = v;
    }
  }
  if (highsOut) *highsOut = highs;
  return flips;
}

static void probeMicBitbang() {
  i2sStop();
  pinMode(PIN_MIC_BCLK, OUTPUT);
  pinMode(PIN_MIC_WS, OUTPUT);
  pinMode(PIN_MIC_SD, INPUT);
  digitalWrite(PIN_MIC_BCLK, LOW);
  digitalWrite(PIN_MIC_WS, LOW);

  uint32_t flips = 0, highs = 0;
  int last = digitalRead(PIN_MIC_SD);
  for (int frame = 0; frame < 4000; ++frame) {
    digitalWrite(PIN_MIC_WS, (frame & 1) ? HIGH : LOW);
    for (int b = 0; b < 32; ++b) {
      digitalWrite(PIN_MIC_BCLK, HIGH);
      const int v = digitalRead(PIN_MIC_SD);
      if (v) highs++;
      if (v != last) {
        flips++;
        last = v;
      }
      digitalWrite(PIN_MIC_BCLK, LOW);
    }
  }
  Serial.printf("micbang: OUT flips=%u highs=%u (GPIO bit-bang clocks on D2/D3)\n",
                (unsigned)flips, (unsigned)highs);
  if (flips == 0 && highs == 0) {
    Serial.println("micbang: D4 stuck LOW — OUT not connected or mic unpowered");
  } else if (flips == 0) {
    Serial.println("micbang: D4 stuck HIGH — no I2S data (check OUT vs BCLK swap)");
  } else {
    Serial.println("micbang: mic OUT is alive — I2S capture can be fixed in software");
  }
}

static void probeMicWire() {
  if (!startMic()) return;

  uint32_t bclkHighs = 0, wsHighs = 0, sdHighs = 0;
  const uint32_t bclkFlips =
      countGpioFlips(i2sGpio(PIN_MIC_BCLK), 30, &bclkHighs);
  const uint32_t wsFlips = countGpioFlips(i2sGpio(PIN_MIC_WS), 30, &wsHighs);

  const gpio_num_t sd = i2sGpio(PIN_MIC_SD);
  gpio_reset_pin(sd);
  gpio_set_direction(sd, GPIO_MODE_INPUT);
  gpio_set_pull_mode(sd, GPIO_FLOATING);
  const uint32_t sdFlips = countGpioFlips(sd, 200, &sdHighs);

  stopMic();
  pinMode(PIN_MIC_SD, INPUT);
  Serial.printf("micwire: BCLK gpio %d flips=%u\n", (int)i2sGpio(PIN_MIC_BCLK),
                (unsigned)bclkFlips);
  Serial.printf("micwire: WS   gpio %d flips=%u\n", (int)i2sGpio(PIN_MIC_WS),
                (unsigned)wsFlips);
  Serial.printf("micwire: OUT  gpio %d flips=%u highs=%u\n", (int)sd,
                (unsigned)sdFlips, (unsigned)sdHighs);
  if (bclkFlips < 10 || wsFlips < 2) {
    Serial.println("micwire: clocks are not on D2/D3 — pin numbering / wiring");
  } else if (sdFlips == 0) {
    Serial.println("micwire: clocks OK, OUT idle — ICS-43434 power or OUT≠D4");
    Serial.println("  3V3 on 3V, GND, BCLK=D3, LRCL=D2, OUT=D4, SEL=GND");
  } else {
    Serial.println("micwire: OUT is toggling — I2S capture path is the issue");
  }
}

static bool startSpk() {
  i2sStop();
  const i2s_config_t cfg = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = true,
  };
  esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);
  if (err != ESP_OK) {
    Serial.printf("spk i2s install err %d\n", (int)err);
    return false;
  }
  const i2s_pin_config_t pins = {.mck_io_num = I2S_PIN_NO_CHANGE,
                                 .bck_io_num = i2sGpio(PIN_SPK_BCLK),
                                 .ws_io_num = i2sGpio(PIN_SPK_WS),
                                 .data_out_num = i2sGpio(PIN_SPK_DIN),
                                 .data_in_num = I2S_PIN_NO_CHANGE};
  err = i2s_set_pin(I2S_PORT, &pins);
  if (err != ESP_OK) {
    Serial.printf("spk i2s pin err %d\n", (int)err);
    i2sStop();
    return false;
  }
  err = i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT,
                    I2S_CHANNEL_STEREO);
  if (err != ESP_OK) {
    Serial.printf("spk i2s clk err %d\n", (int)err);
    i2sStop();
    return false;
  }
  i2s_zero_dma_buffer(I2S_PORT);
  i2s_start(I2S_PORT);
  return true;
}

static void stopSpk() { i2sStop(); }

static size_t i2sWriteAll(const uint8_t *data, size_t len) {
  size_t off = 0;
  while (off < len) {
    size_t written = 0;
    i2s_write(I2S_PORT, data + off, len - off, &written, portMAX_DELAY);
    if (written == 0) break;
    off += written;
  }
  return off;
}

static void primeSpkSilence() {
  static int32_t silence[256];
  memset(silence, 0, sizeof(silence));
  for (int p = 0; p < 4; ++p) {
    i2sWriteAll((uint8_t *)silence, sizeof(silence));
  }
}

static float smoothstep(float t) {
  if (t <= 0.f) return 0.f;
  if (t >= 1.f) return 1.f;
  return t * t * (3.f - 2.f * t);
}

static float noteEnvelope(float t, float dur, float attack, float release) {
  if (dur <= 0.f) return 0.f;
  if (t < attack) return smoothstep(t / attack);
  const float relStart = dur - release;
  if (t > relStart) {
    return 1.f - smoothstep((t - relStart) / fmaxf(release, 0.001f));
  }
  return 1.f;
}

static void ampUnmuteAfterPrime() {
  delay(30);
  ampMute(false);
  delay(20);
}

static void writeStereoMono(const int16_t *mono, size_t n, bool applyVol = true) {
  int32_t stereo[256];
  size_t i = 0;
  while (i < n) {
    thinkingTick();
    if (applyVol) updateVolume();
    const size_t chunk = min(n - i, (size_t)128);
    for (size_t k = 0; k < chunk; ++k) {
      const int16_t s = applyVol ? scaleSample(mono[i + k]) : mono[i + k];
      const int32_t w = (int32_t)s << 16;
      stereo[k * 2] = w;
      stereo[k * 2 + 1] = w;
    }
    i2sWriteAll((uint8_t *)stereo, chunk * 8);
    i += chunk;
  }
}

static void playNoteFreq(int hz, int ms, float seqGainStart = 1.f,
                         float seqGainEnd = 1.f, float releaseSec = 0.05f) {
  const int total = SAMPLE_RATE * ms / 1000;
  const float noteSec = (float)ms / 1000.f;
  const float attackSec = 0.022f;
  int16_t buf[128];
  for (int off = 0; off < total; off += 128) {
    const int count = min(128, total - off);
    for (int i = 0; i < count; ++i) {
      const float t = (float)(off + i) / (float)SAMPLE_RATE;
      const float noteEnv =
          noteEnvelope(t, noteSec, attackSec, releaseSec);
      const float seqGain =
          seqGainStart + (seqGainEnd - seqGainStart) * (t / noteSec);
      const float w = 2.f * (float)M_PI * (float)hz * t;
      const float s = sinf(w) * noteEnv * seqGain;
      buf[i] = (int16_t)(s * 9500.f);
    }
    writeStereoMono(buf, (size_t)count);
  }
}

static void playNoteGap(int ms) {
  static int16_t silence[128];
  memset(silence, 0, sizeof(silence));
  const int total = SAMPLE_RATE * ms / 1000;
  for (int off = 0; off < total; off += 128) {
    const int count = min(128, total - off);
    writeStereoMono(silence, (size_t)count);
  }
}

static void ampMuteAfterTail() {
  primeSpkSilence();
  playNoteGap(80);
  ampMute(true);
}

static void playBeep(int hz, int ms, bool ampOn = true) {
  if (!startSpk()) return;
  primeSpkSilence();
  playNoteGap(40);
  if (ampOn) ampUnmuteAfterPrime();
  playNoteFreq(hz, ms, 1.f, 1.f, fmaxf((float)ms / 1000.f * 0.2f, 0.06f));
  if (ampOn) {
    ampMuteAfterTail();
  } else {
    primeSpkSilence();
    playNoteGap(80);
  }
  stopSpk();
}

static void playTone(bool ampOn = true) {
  Serial.println("tone: i2s");
  if (!startSpk()) return;
  primeSpkSilence();
  playNoteGap(40);
  if (ampOn) ampUnmuteAfterPrime();

  static const struct {
    int hz;
    int ms;
  } notes[] = {
      {262, 220}, {330, 220}, {392, 260}, {523, 320}, {659, 480},
  };
  const size_t nNotes = sizeof(notes) / sizeof(notes[0]);
  for (size_t i = 0; i < nNotes; ++i) {
    const float seqStart = (i == 0) ? 0.f : 1.f;
    const float seqEnd = (i == nNotes - 1) ? 0.f : 1.f;
    const float release =
        (i == nNotes - 1) ? 0.18f : fminf((float)notes[i].ms / 1000.f * 0.25f, 0.07f);
    playNoteFreq(notes[i].hz, notes[i].ms, seqStart, seqEnd, release);
    if (i + 1 < nNotes) playNoteGap(25);
  }

  if (ampOn) {
    ampMuteAfterTail();
  } else {
    primeSpkSilence();
    playNoteGap(80);
  }
  stopSpk();
  Serial.println("tone: done");
}

static bool pttHeld() { return digitalRead(PIN_PTT) == LOW; }

static int16_t decodeMicWord(int32_t w) { return (int16_t)(w >> 14); }

static size_t recordPtt() {
  Serial.println("ptt: recording (hold, then release)");
  ampMute(true);
  digitalWrite(LED_BUILTIN, HIGH);
  if (!startMic()) {
    digitalWrite(LED_BUILTIN, LOW);
    Serial.println("ptt: mic start failed");
    return 0;
  }

  size_t samples = 0;
  const uint32_t t0 = millis();
  int32_t raw[64];
  uint64_t acc = 0;
  int reads = 0;
  int empty = 0;
  bool dumped = false;

  while (samples < gMaxSamples) {
    const uint32_t elapsed = millis() - t0;
    if (elapsed >= MIN_RECORD_MS && !pttHeld()) break;
    if (elapsed >= MAX_RECORD_MS) break;

    size_t bytesRead = 0;
    if (i2s_read(I2S_PORT, raw, sizeof(raw), &bytesRead, pdMS_TO_TICKS(50)) !=
            ESP_OK ||
        bytesRead < 4) {
      empty++;
      continue;
    }
    reads++;
    const int n = (int)bytesRead / 4;
    if (!dumped && n >= 4) {
      Serial.printf("ptt: raw %08x %08x %08x %08x\n", (unsigned)raw[0],
                    (unsigned)raw[1], (unsigned)raw[2], (unsigned)raw[3]);
      dumped = true;
    }
    for (int i = 0; i < n && samples < gMaxSamples; ++i) {
      const int16_t s = decodeMicWord(raw[i]);
      gPcm[samples++] = s;
      acc += (int32_t)s * (int32_t)s;
    }
  }

  stopMic();
  digitalWrite(LED_BUILTIN, LOW);
  const float rms = samples ? sqrtf((float)acc / (float)samples) : 0.f;
  Serial.printf("recorded %u samples, rms %.1f (reads %d empty %d)\n",
                (unsigned)samples, rms, reads, empty);
  if (samples > 0 && rms < 20.f) {
    Serial.println("ptt: mic nearly silent — check ICS-43434 3V3/GND/BCLK/WS/OUT");
  }
  return samples;
}

static bool readLine(WiFiClient &c, String &line, uint32_t timeoutMs) {
  line = "";
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (c.available()) {
      const char ch = (char)c.read();
      if (ch == '\n') {
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        return true;
      }
      line += ch;
      if (line.length() > 2048) return false;
    }
    if (!c.connected() && !c.available()) return false;
    thinkingTick();
    delay(1);
  }
  return false;
}

static bool readExact(WiFiClient &c, uint8_t *dst, size_t n, uint32_t timeoutMs) {
  size_t got = 0;
  const uint32_t start = millis();
  while (got < n) {
    if (millis() - start > timeoutMs) return false;
    const int avail = c.available();
    if (avail > 0) {
      const size_t want = min((size_t)avail, n - got);
      const int r = c.read(dst + got, want);
      if (r > 0) got += (size_t)r;
    } else {
      if (!c.connected()) return false;
      thinkingTick();
      delay(1);
    }
  }
  return true;
}

static int parseHttpStatus(const String &line) {
  const int afterVer = line.indexOf(' ');
  if (afterVer < 0) return 0;
  const int codeEnd = line.indexOf(' ', afterVer + 1);
  if (codeEnd < 0) return line.substring(afterVer + 1).toInt();
  return line.substring(afterVer + 1, codeEnd).toInt();
}

static bool readHeaders(WiFiClient &c, HttpHeaders &h, uint32_t timeoutMs) {
  String line;
  if (!readLine(c, line, timeoutMs)) return false;
  h.status = parseHttpStatus(line);

  for (;;) {
    if (!readLine(c, line, HTTP_TIMEOUT_MS)) return false;
    if (line.length() == 0) break;
    const int colon = line.indexOf(':');
    if (colon < 0) continue;
    String name = line.substring(0, colon);
    String value = line.substring(colon + 1);
    name.trim();
    value.trim();
    name.toLowerCase();
    if (name == "transfer-encoding" && value.indexOf("chunked") >= 0) {
      h.chunked = true;
    } else if (name == "content-length") {
      h.contentLength = value.toInt();
    } else if (name == "x-turn-id") {
      h.turnId = value;
    } else if (name == "x-transcript") {
      h.transcript = value;
    } else if (name == "x-alfred-error") {
      h.alfredError = value;
    } else if (name == "x-fast-path") {
      h.fastPath = value == "1" || value == "true";
    }
  }
  return true;
}

static PlayResult writeMonoBytesToSpk(const uint8_t *data, size_t len) {
  gPlayBytes += len;
  size_t i = 0;
  while (i + 1 < len) {
    if (pttHeld()) return PlayResult::BargeIn;
    const size_t remainSamples = (len - i) / 2;
    const size_t n = min(remainSamples, (size_t)128);
    writeStereoMono((const int16_t *)(data + i), n);
    i += n * 2;
  }
  return PlayResult::Done;
}

static PlayResult playChunked(WiFiClient &c) {
  uint8_t buf[1024];
  for (;;) {
    if (pttHeld()) return PlayResult::BargeIn;
    String line;
    if (!readLine(c, line, AUDIO_WAIT_MS)) return PlayResult::Error;
    const unsigned long chunk = strtoul(line.c_str(), nullptr, 16);
    if (chunk == 0) {
      readLine(c, line, HTTP_TIMEOUT_MS);
      return PlayResult::Done;
    }
    size_t left = (size_t)chunk;
    while (left > 0) {
      const size_t n = min(left, sizeof(buf));
      if (!readExact(c, buf, n, HTTP_TIMEOUT_MS)) return PlayResult::Error;
      const PlayResult pr = writeMonoBytesToSpk(buf, n);
      if (pr != PlayResult::Done) return pr;
      left -= n;
    }
    if (!readLine(c, line, HTTP_TIMEOUT_MS)) return PlayResult::Error;
  }
}

static PlayResult playIdentity(WiFiClient &c, int contentLength) {
  uint8_t buf[1024];
  int left = contentLength;
  while (left > 0) {
    if (pttHeld()) return PlayResult::BargeIn;
    const size_t n = min((size_t)left, sizeof(buf));
    if (!readExact(c, buf, n, HTTP_TIMEOUT_MS)) return PlayResult::Error;
    const PlayResult pr = writeMonoBytesToSpk(buf, n);
    if (pr != PlayResult::Done) return pr;
    left -= (int)n;
  }
  return PlayResult::Done;
}

static PlayResult playUntilClose(WiFiClient &c) {
  uint8_t buf[1024];
  for (;;) {
    if (pttHeld()) return PlayResult::BargeIn;
    const uint32_t start = millis();
    while (!c.available()) {
      if (pttHeld()) return PlayResult::BargeIn;
      if (!c.connected()) return PlayResult::Done;
      if (millis() - start > AUDIO_WAIT_MS) return PlayResult::Error;
      thinkingTick();
      delay(1);
    }
    const int n = c.read(buf, sizeof(buf));
    if (n <= 0) return c.connected() ? PlayResult::Done : PlayResult::Done;
    const PlayResult pr = writeMonoBytesToSpk(buf, (size_t)n);
    if (pr != PlayResult::Done) return pr;
  }
}

static void writeAuthHeaders(WiFiClient &c) {
  c.printf("Host: %s:%d\r\n", ALFRED_HOST, ALFRED_PORT);
  c.printf("Authorization: Bearer %s\r\n", ALFRED_TOKEN);
  c.printf("X-Device-Id: %s\r\n", gDeviceId.c_str());
}

static bool connectAlfred(WiFiClient &c) {
  c.setTimeout(HEADER_TIMEOUT_MS);
  if (!c.connect(ALFRED_HOST, ALFRED_PORT, HTTP_TIMEOUT_MS)) {
    Serial.printf("connect failed %s:%d\n", ALFRED_HOST, ALFRED_PORT);
    return false;
  }
  return true;
}

static PlayResult playResponseBody(WiFiClient &c, const HttpHeaders &h) {
  Serial.printf("play: wait up to %ds (chunked=%d len=%d)\n", AUDIO_WAIT_MS / 1000,
                (int)h.chunked, h.contentLength);
  gPlayBytes = 0;
  if (!startSpk()) return PlayResult::Error;
  primeSpkSilence();
  ampUnmuteAfterPrime();
  PlayResult pr;
  if (h.chunked) {
    pr = playChunked(c);
  } else if (h.contentLength >= 0) {
    pr = playIdentity(c, h.contentLength);
  } else {
    pr = playUntilClose(c);
  }
  ampMute(true);
  stopSpk();
  const char *rs = pr == PlayResult::Done      ? "done"
                   : pr == PlayResult::BargeIn ? "barge-in"
                                               : "error";
  Serial.printf("play: %u bytes %s\n", (unsigned)gPlayBytes, rs);
  if (pr == PlayResult::Done && gPlayBytes < 64) {
    Serial.println("play: empty audio — Arbiter/TTS produced no PCM");
  }
  return pr;
}

static void printHttpMeta(const HttpHeaders &h) {
  Serial.printf("HTTP %d turn=%s\n", h.status, h.turnId.c_str());
  if (h.fastPath) Serial.println("fast-path: 1");
  if (h.transcript.length()) {
    Serial.printf("transcript: %s\n", h.transcript.c_str());
  }
  if (h.alfredError.length()) {
    Serial.printf("error: %s\n", h.alfredError.c_str());
  }
}

static void dumpHttpBody(WiFiClient &c, const HttpHeaders &h) {
  if (h.chunked) {
    String line;
    for (;;) {
      if (!readLine(c, line, HTTP_TIMEOUT_MS)) break;
      const unsigned long chunk = strtoul(line.c_str(), nullptr, 16);
      if (chunk == 0) break;
      String body;
      body.reserve((unsigned)chunk);
      for (unsigned long i = 0; i < chunk; ++i) {
        const uint32_t start = millis();
        while (!c.available()) {
          if (millis() - start > HTTP_TIMEOUT_MS) {
            if (body.length()) Serial.println(body);
            return;
          }
          delay(1);
        }
        body += (char)c.read();
        if (body.length() > 1500) {
          Serial.println(body);
          body = "";
        }
      }
      if (body.length()) Serial.println(body);
      readLine(c, line, HTTP_TIMEOUT_MS);
    }
    return;
  }
  if (h.contentLength > 0) {
    const int n = min(h.contentLength, 2048);
    String body;
    body.reserve(n);
    uint8_t buf[256];
    int left = n;
    while (left > 0) {
      const size_t want = min((size_t)left, sizeof(buf));
      if (!readExact(c, buf, want, HTTP_TIMEOUT_MS)) break;
      for (size_t i = 0; i < want; ++i) body += (char)buf[i];
      left -= (int)want;
    }
    Serial.println(body);
    return;
  }
  String line;
  while (readLine(c, line, 2000)) {
    Serial.println(line);
  }
}

static void cancelTurn(const String &turnId) {
  if (turnId.isEmpty()) return;
  WiFiClient c;
  if (!connectAlfred(c)) return;
  c.printf("POST /v1/turns/%s/cancel HTTP/1.1\r\n", turnId.c_str());
  writeAuthHeaders(c);
  c.print("Content-Length: 0\r\nConnection: close\r\n\r\n");
  HttpHeaders h;
  readHeaders(c, h, HTTP_TIMEOUT_MS);
  c.stop();
  Serial.printf("cancel status %d\n", h.status);
}

static PlayResult postUtterance(const int16_t *pcm, size_t samples) {
  WiFiClient c;
  if (!connectAlfred(c)) return PlayResult::Error;

  const size_t bytes = samples * sizeof(int16_t);
  c.print("POST /v1/utterance HTTP/1.1\r\n");
  writeAuthHeaders(c);
  c.print("Content-Type: audio/L16; rate=16000; channels=1\r\n");
  c.printf("Content-Length: %u\r\n", (unsigned)bytes);
  c.print("Connection: close\r\n\r\n");

  const uint8_t *p = (const uint8_t *)pcm;
  size_t left = bytes;
  while (left > 0) {
    const size_t n = min(left, (size_t)1024);
    if (c.write(p, n) != n) {
      Serial.println("pcm write failed");
      c.stop();
      return PlayResult::Error;
    }
    p += n;
    left -= n;
    yield();
  }

  Thinking think;
  digitalWrite(LED_BUILTIN, HIGH);
  HttpHeaders h;
  if (!readHeaders(c, h, HEADER_TIMEOUT_MS)) {
    Serial.println("no response headers (is alfred.json listen_host reachable?)");
    digitalWrite(LED_BUILTIN, LOW);
    c.stop();
    return PlayResult::Error;
  }
  digitalWrite(LED_BUILTIN, LOW);
  gTurnId = h.turnId;
  printHttpMeta(h);
  if (h.status != 200) {
    dumpHttpBody(c, h);
    c.stop();
    return PlayResult::Error;
  }

  const PlayResult pr = playResponseBody(c, h);
  c.stop();
  return pr;
}

static String jsonEscape(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (unsigned i = 0; i < s.length(); ++i) {
    const char c = s[i];
    if (c == '"' || c == '\\') {
      o += '\\';
      o += c;
    } else if (c == '\n') {
      o += "\\n";
    } else if (c == '\r') {
      o += "\\r";
    } else {
      o += c;
    }
  }
  return o;
}

static PlayResult postText(const String &text) {
  WiFiClient c;
  if (!connectAlfred(c)) return PlayResult::Error;
  String body = String("{\"text\":\"") + jsonEscape(text) + "\"}";
  c.print("POST /v1/utterance/text HTTP/1.1\r\n");
  writeAuthHeaders(c);
  c.print("Content-Type: application/json\r\n");
  c.printf("Content-Length: %u\r\n", (unsigned)body.length());
  c.print("Connection: close\r\n\r\n");
  c.print(body);

  Thinking think;
  HttpHeaders h;
  if (!readHeaders(c, h, HEADER_TIMEOUT_MS)) {
    c.stop();
    return PlayResult::Error;
  }
  gTurnId = h.turnId;
  printHttpMeta(h);
  if (h.status != 200) {
    dumpHttpBody(c, h);
    c.stop();
    return PlayResult::Error;
  }
  const PlayResult pr = playResponseBody(c, h);
  c.stop();
  return pr;
}

static void getHealth() {
  WiFiClient c;
  if (!connectAlfred(c)) return;
  c.print("GET /health HTTP/1.1\r\n");
  c.printf("Host: %s:%d\r\n", ALFRED_HOST, ALFRED_PORT);
  c.print("Connection: close\r\n\r\n");
  c.flush();
  HttpHeaders h;
  if (!readHeaders(c, h, HTTP_TIMEOUT_MS)) {
    Serial.println("health: no headers");
    c.stop();
    return;
  }
  Serial.printf("health HTTP %d\n", h.status);
  String line;
  while (readLine(c, line, 2000)) {
    Serial.println(line);
  }
  if (h.status == 200) {
    Serial.println("health: ok (alfred reachable)");
  } else if (h.status == 503) {
    Serial.println("health: alfred up, whisper/piper not ready");
  } else {
    Serial.printf("health: unexpected (check alfred %s:%d)\n", ALFRED_HOST,
                  ALFRED_PORT);
  }
  c.stop();
}

static void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (!line.length()) return;
  String cmd = line;
  cmd.toLowerCase();
  if (cmd == "say" || cmd.startsWith("say ")) {
    String text = cmd == "say" ? "" : line.substring(4);
    text.trim();
    if (!text.length()) {
      Serial.println("usage: say <text>");
      Serial.println("  say echo hello there    (TTS only, skip Arbiter)");
      Serial.println("  say what time is it     (fast path or Arbiter)");
      return;
    }
    Serial.printf("say: %s\n", text.c_str());
    postText(text);
    return;
  }
  if (cmd == "tone") {
    playTone(true);
  } else if (cmd == "beep") {
    playBeep(440, 800, true);
  } else if (cmd == "tonemute") {
    playTone(false);
    Serial.println("tonemute: amp stayed muted (i2s-only test)");
  } else if (cmd == "health") {
    getHealth();
  } else if (cmd == "micwire") {
    probeMicWire();
  } else if (cmd == "micbang") {
    probeMicBitbang();
  } else if (cmd == "clkblink") {
    i2sStop();
    pinMode(PIN_MIC_BCLK, OUTPUT);
    pinMode(PIN_MIC_WS, OUTPUT);
    Serial.println("clkblink: D3 then D2 toggle 1Hz — meter should bounce 0/3.3V");
    for (int i = 0; i < 8; ++i) {
      digitalWrite(PIN_MIC_BCLK, HIGH);
      Serial.println("D3 HIGH");
      delay(500);
      digitalWrite(PIN_MIC_BCLK, LOW);
      Serial.println("D3 LOW");
      delay(500);
    }
    for (int i = 0; i < 8; ++i) {
      digitalWrite(PIN_MIC_WS, HIGH);
      Serial.println("D2 HIGH");
      delay(500);
      digitalWrite(PIN_MIC_WS, LOW);
      Serial.println("D2 LOW");
      delay(500);
    }
  } else if (cmd == "mic") {
    Serial.println("mic: recording 1s (no PTT)");
    ampMute(true);
    if (!startMic()) return;
    size_t samples = 0;
    uint64_t acc = 0;
    const uint32_t t0 = millis();
    int32_t raw[64];
    bool dumped = false;
    while (millis() - t0 < 1000 && samples < gMaxSamples) {
      size_t bytesRead = 0;
      if (i2s_read(I2S_PORT, raw, sizeof(raw), &bytesRead, pdMS_TO_TICKS(50)) !=
              ESP_OK ||
          bytesRead < 4) {
        continue;
      }
      const int n = (int)bytesRead / 4;
      if (!dumped && n >= 4) {
        Serial.printf("mic: raw %08x %08x %08x %08x\n", (unsigned)raw[0],
                      (unsigned)raw[1], (unsigned)raw[2], (unsigned)raw[3]);
        dumped = true;
      }
      for (int i = 0; i < n && samples < gMaxSamples; ++i) {
        const int16_t s = decodeMicWord(raw[i]);
        gPcm[samples++] = s;
        acc += (int32_t)s * (int32_t)s;
      }
    }
    stopMic();
    const float rms = samples ? sqrtf((float)acc / (float)samples) : 0.f;
    Serial.printf("mic: %u samples, rms %.1f (speak — want >> 50)\n",
                  (unsigned)samples, rms);
  } else if (cmd == "ping") {
    postText("ping");
  } else if (cmd == "status") {
    postText("status");
  } else if (cmd == "vol") {
    updateVolume();
    Serial.printf("vol adc=%d gain=%d (4095=full)\n", analogRead(PIN_VOLUME),
                  gVolQ15);
  } else if (cmd == "pins") {
    Serial.printf("spk bck=%d ws=%d din=%d sd=%d mic bck=%d ws=%d sd=%d\n",
                  i2sGpio(PIN_SPK_BCLK), i2sGpio(PIN_SPK_WS),
                  i2sGpio(PIN_SPK_DIN), i2sGpio(PIN_AMP_SD),
                  i2sGpio(PIN_MIC_BCLK), i2sGpio(PIN_MIC_WS),
                  i2sGpio(PIN_MIC_SD));
  } else if (cmd == "click") {
    const gpio_num_t g = ampGpio();
    Serial.printf("click: sd gpio=%d — toggling 3x (400ms on/off)\n", g);
    for (int i = 0; i < 3; ++i) {
      gpio_set_level(g, 1);
      Serial.println("click: SD HIGH");
      delay(400);
      gpio_set_level(g, 0);
      Serial.println("click: SD LOW");
      delay(400);
    }
  } else if (cmd == "ampon") {
    ampMute(false);
    Serial.printf("ampon: sd gpio=%d left HIGH — try beep\n", ampGpio());
  } else if (cmd == "ampoff") {
    ampMute(true);
    Serial.println("ampoff: SD LOW");
  } else if (cmd.length()) {
    Serial.println(
        "commands: say <text> | ping | health | tone | beep | vol");
  }
}

static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("wifi %s", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    Serial.print('.');
    if (millis() - t0 > 30000) {
      Serial.println(" failed (continuing offline — tone still works)");
      digitalWrite(LED_BUILTIN, LOW);
      return;
    }
  }
  digitalWrite(LED_BUILTIN, LOW);
  Serial.printf("\nip %s\n", WiFi.localIP().toString().c_str());
}

static const char *resetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic/crash";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "other";
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_THINK_LED, OUTPUT);
  pinMode(PIN_PTT, INPUT_PULLUP);
  ampInit();
  pinMode(PIN_VOLUME, INPUT);
  analogReadResolution(12);
  thinkLed(false);
  updateVolume();

  Serial.begin(57600);
  delay(500);
  Serial.println("\nalfred nano-esp32 endpoint");
  Serial.println(FIRMWARE_CONFIG_TAG);
  Serial.printf("reset: %s\n", resetReasonStr());
  if (pttHeld()) {
    Serial.println("PTT is LOW — release button before talking (stuck PTT causes reboot loops)");
  }

  if (!allocCapture()) {
    Serial.println("capture alloc failed (enable PSRAM)");
    dieBlink();
  }

  connectWifi();

  if (String(ALFRED_DEVICE_ID).length()) {
    gDeviceId = ALFRED_DEVICE_ID;
  } else {
    gDeviceId = WiFi.macAddress();
    gDeviceId.replace(":", "");
    gDeviceId.toLowerCase();
    gDeviceId = "nano-" + gDeviceId;
  }
  Serial.printf("device-id %s\n", gDeviceId.c_str());
  Serial.printf("alfred %s:%d\n", ALFRED_HOST, ALFRED_PORT);
  Serial.printf("volume adc=%d gain=%d\n", analogRead(PIN_VOLUME), gVolQ15);
  Serial.printf("spk i2s gpio bck=%d ws=%d din=%d sd=%d\n", i2sGpio(PIN_SPK_BCLK),
                i2sGpio(PIN_SPK_WS), i2sGpio(PIN_SPK_DIN), i2sGpio(PIN_AMP_SD));
  if (gVolQ15 < 512) {
    Serial.println("volume at minimum — turn A0 pot CW for Alfred playback");
    Serial.println("(tone/beep ignore pot; use beep to test speaker)");
  }
  Serial.println("serial: say <text> | ping | health | tone");
}

void loop() {
  handleSerial();
  updatePttLed();

  if (!gPttArmed) {
    if (!pttHeld()) gPttArmed = true;
    else delay(10);
    return;
  }

  if (!pttHeld()) {
    delay(10);
    return;
  }
  delay(30);
  if (!pttHeld()) return;

  const size_t n = recordPtt();
  if (n < (SAMPLE_RATE / 10)) {
    Serial.printf("too short (%u samples) — hold PTT ~1s and speak\n",
                  (unsigned)n);
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("no wifi — fix credentials or use serial: tone");
    return;
  }

  PlayResult pr = postUtterance(gPcm, n);
  while (pr == PlayResult::BargeIn) {
    Serial.println("barge-in");
    cancelTurn(gTurnId);
    delay(30);
    const size_t n2 = recordPtt();
    if (n2 < (SAMPLE_RATE / 10)) break;
    pr = postUtterance(gPcm, n2);
  }
}
