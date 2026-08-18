#include <WiFi.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "ESP_I2S.h"
#include "config.h"

// Thin PCM endpoint for Alfred (docs/device.md):
// hold PTT -> record 16 kHz s16le -> POST /v1/utterance -> play chunked PCM.
// Serial (idle): tone | health | ping

// PTT is sequential (record XOR play), so one I2S controller is enough.
// Dual I2S0+I2S1 is only needed later for listen-while-speaking duplex.
static I2SClass i2s;

static String gDeviceId;
static int16_t *gPcm = nullptr;
static size_t gMaxSamples = 0;
static String gTurnId;

enum class PlayResult { Done, BargeIn, Error };

static void ampMute(bool mute) { digitalWrite(PIN_AMP_SD, mute ? LOW : HIGH); }

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
  i2s.setPins((int8_t)PIN_MIC_BCLK, (int8_t)PIN_MIC_WS, -1, (int8_t)PIN_MIC_SD);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT,
                 I2S_SLOT_MODE_STEREO)) {
    Serial.println("mic I2S begin failed");
    return false;
  }
  if (!i2s.configureRX(SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO,
                       I2S_RX_TRANSFORM_32_TO_16)) {
    Serial.println("mic 32->16 transform failed");
    i2s.end();
    return false;
  }
  return true;
}

static void stopMic() { i2s.end(); }

static bool startSpk() {
  i2s.setPins((int8_t)PIN_SPK_BCLK, (int8_t)PIN_SPK_WS, (int8_t)PIN_SPK_DIN, -1);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_STEREO)) {
    Serial.println("spk I2S begin failed");
    return false;
  }
  return true;
}

static void stopSpk() { i2s.end(); }

static void writeStereoMono(const int16_t *mono, size_t n) {
  int16_t stereo[256];
  size_t i = 0;
  while (i < n) {
    const size_t chunk = min(n - i, (size_t)128);
    for (size_t k = 0; k < chunk; ++k) {
      stereo[k * 2] = mono[i + k];
      stereo[k * 2 + 1] = mono[i + k];
    }
    i2s.write((uint8_t *)stereo, chunk * 4);
    i += chunk;
  }
}

static void playTone(int hz, int ms) {
  if (!startSpk()) return;
  ampMute(false);
  delay(15);
  const int n = SAMPLE_RATE * ms / 1000;
  int16_t buf[128];
  for (int off = 0; off < n; off += 128) {
    const int count = min(128, n - off);
    for (int i = 0; i < count; ++i) {
      const float t = (float)(off + i) / (float)SAMPLE_RATE;
      buf[i] = (int16_t)(sinf(2.f * (float)M_PI * (float)hz * t) * 6000.f);
    }
    writeStereoMono(buf, (size_t)count);
  }
  ampMute(true);
  stopSpk();
}

static bool pttHeld() { return digitalRead(PIN_PTT) == LOW; }

static size_t recordPtt() {
  ampMute(true);
  digitalWrite(LED_BUILTIN, HIGH);
  if (!startMic()) {
    digitalWrite(LED_BUILTIN, LOW);
    return 0;
  }

  size_t samples = 0;
  const uint32_t t0 = millis();
  int16_t stereo[256];
  uint64_t acc = 0;

  while (samples < gMaxSamples) {
    const uint32_t elapsed = millis() - t0;
    if (elapsed >= MIN_RECORD_MS && !pttHeld()) break;
    if (elapsed >= MAX_RECORD_MS) break;

    const int n = i2s.readBytes((char *)stereo, sizeof(stereo));
    if (n < 4) continue;
    const int frames = n / 4;
    for (int i = 0; i < frames && samples < gMaxSamples; ++i) {
      const int16_t s = MIC_LEFT_SLOT ? stereo[i * 2] : stereo[i * 2 + 1];
      gPcm[samples++] = s;
      acc += (int32_t)s * (int32_t)s;
    }
  }

  stopMic();
  digitalWrite(LED_BUILTIN, LOW);
  if (samples > 0) {
    const float rms = sqrtf((float)acc / (float)samples);
    Serial.printf("recorded %u samples, rms %.1f\n", (unsigned)samples, rms);
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
      delay(1);
    }
  }
  return true;
}

struct HttpHeaders {
  int status = 0;
  bool chunked = false;
  int contentLength = -1;
  String turnId;
  String transcript;
};

static bool readHeaders(WiFiClient &c, HttpHeaders &h, uint32_t timeoutMs) {
  String line;
  if (!readLine(c, line, timeoutMs)) return false;
  const int sp = line.indexOf(' ');
  if (sp < 0) return false;
  h.status = line.substring(sp + 1).toInt();

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
    }
  }
  return true;
}

static PlayResult writeMonoBytesToSpk(const uint8_t *data, size_t len) {
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
    if (!readLine(c, line, HTTP_TIMEOUT_MS)) return PlayResult::Error;
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
      if (millis() - start > HTTP_TIMEOUT_MS) return PlayResult::Error;
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
  if (!startSpk()) return PlayResult::Error;
  ampMute(false);
  delay(15);
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
  return pr;
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
  Serial.printf("HTTP %d turn=%s transcript=%s\n", h.status, h.turnId.c_str(),
                h.transcript.c_str());
  if (h.status != 200) {
    String err;
    readLine(c, err, HTTP_TIMEOUT_MS);
    Serial.println(err);
    c.stop();
    return PlayResult::Error;
  }

  const PlayResult pr = playResponseBody(c, h);
  c.stop();
  return pr;
}

static PlayResult postText(const char *text) {
  WiFiClient c;
  if (!connectAlfred(c)) return PlayResult::Error;
  String body = String("{\"text\":\"") + text + "\"}";
  c.print("POST /v1/utterance/text HTTP/1.1\r\n");
  writeAuthHeaders(c);
  c.print("Content-Type: application/json\r\n");
  c.printf("Content-Length: %u\r\n", (unsigned)body.length());
  c.print("Connection: close\r\n\r\n");
  c.print(body);

  HttpHeaders h;
  if (!readHeaders(c, h, HEADER_TIMEOUT_MS)) {
    c.stop();
    return PlayResult::Error;
  }
  gTurnId = h.turnId;
  Serial.printf("HTTP %d turn=%s\n", h.status, h.turnId.c_str());
  if (h.status != 200) {
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
  c.stop();
}

static void handleSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();
  if (cmd == "tone") {
    playTone(440, 1000);
  } else if (cmd == "health") {
    getHealth();
  } else if (cmd == "ping") {
    postText("ping");
  } else if (cmd == "status") {
    postText("status");
  } else if (cmd.length()) {
    Serial.println("commands: tone | health | ping | status");
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
      Serial.println(" failed");
      dieBlink();
    }
  }
  digitalWrite(LED_BUILTIN, LOW);
  Serial.printf("\nip %s\n", WiFi.localIP().toString().c_str());
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_PTT, INPUT_PULLUP);
  pinMode(PIN_AMP_SD, OUTPUT);
  ampMute(true);

  Serial.begin(115200);
  delay(200);
  Serial.println("\nalfred nano-esp32 endpoint");

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
  Serial.println("hold PTT to talk; serial: tone | health | ping");
}

void loop() {
  handleSerial();

  if (!pttHeld()) {
    delay(10);
    return;
  }
  delay(30);
  if (!pttHeld()) return;

  const size_t n = recordPtt();
  if (n < (SAMPLE_RATE / 10)) {
    Serial.println("too short");
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
