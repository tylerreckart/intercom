# Intercom

Local-first **voice bridge** for [Arbiter](https://arbiter.run): ESP32 (or any client) speaks PCM in, Intercom runs **whisper.cpp** STT + **Piper**/Kokoro TTS, and Arbiter stays text + SSE in the middle.

```
ESP32  --HTTP PTT PCM-->  Intercom  --text/SSE-->  arbiter --api
ESP32  <--chunked PCM---  Intercom  <--text------/
```

Colocate Intercom on the same host as `arbiter --api` (default `http://127.0.0.1:8080`). Device tokens never see the Arbiter bearer.

The default agent is **Arthur** — a British voice assistant with full Arbiter tool access (`config/arthur.agent.json`).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Requires C++20, CMake 3.20+, SQLite3, Threads. Fetches cpp-httplib and nlohmann/json.

## Configure

```bash
cp config/intercom.example.json intercom.json
# set arbiter_token, paths to whisper-cli + model, piper/kokoro + voice
./build/intercom --config intercom.json
```

Install speech tools separately (not vendored):

- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) → `whisper-cli` + `ggml-base.en.bin`
- [Piper](https://github.com/rhasspy/piper) → `piper` + an `.onnx` voice (e.g. `en_US-lessac-medium`)

## Quick test (no mic)

Fast-path (skips Arbiter):

```bash
curl -N -H "Authorization: Bearer dev-device-secret-change-me" \
  -H "X-Device-Id: speaker-1" \
  -H "Content-Type: application/json" \
  -d '{"text":"what time is it"}' \
  --output reply.pcm \
  http://127.0.0.1:8090/v1/utterance/text
```

Play: `ffplay -f s16le -ar 16000 -ac 1 reply.pcm`

PCM utterance: see [docs/api.md](docs/api.md) and [docs/device.md](docs/device.md).

## License

Apache-2.0
