# Device PCM contract

Target hardware: **ESP32-S3 with PSRAM** (e.g. Arduino Nano ESP32) + I2S mic (INMP441) + I2S amp (MAX98357A). Intercom runs on the server; the MCU is a thin audio endpoint.

## Audio format

| Direction | Format |
|-----------|--------|
| Mic → Intercom | Mono PCM s16le, **24 kHz**, push-to-talk HTTP body |
| Intercom → speaker | Mono PCM s16le, **24 kHz**, chunked HTTP response |

Content-Type: `audio/L16; rate=24000; channels=1`

## Session model

1. Hold a stable `X-Device-Id` (e.g. MAC or flashed UUID).
2. Use the Intercom device bearer (not Arbiter’s token).
3. Intercom maps device → Arbiter `conversation_id` in SQLite (`session_db`).
4. Same device keeps memory/history across boots of the MCU.

## Push-to-talk flow

1. User holds button → record PCM into RAM/PSRAM.
2. `POST /v1/utterance` with PCM body.
3. Read response headers (`X-Turn-Id`, `X-Transcript`).
4. Stream response body to I2S amp as chunks arrive.
5. On barge-in: stop I2S playback and `POST /v1/turns/{X-Turn-Id}/cancel`.

## Colocation

Run Intercom beside `arbiter --api` on the same host:

- `arbiter_base_url`: `http://127.0.0.1:8080`
- Intercom listen: `127.0.0.1:8090` (put TLS/auth on a reverse proxy for LAN/WAN devices)

## Generating a test PCM file

```bash
# 1s of silence (or replace with sox/ffmpeg from mic)
ffmpeg -f lavfi -i "sine=frequency=440:duration=1" -ar 24000 -ac 1 -f s16le utterance.pcm

# Or speak via text path (no whisper needed for path test):
curl -N -H "Authorization: Bearer dev-device-secret-change-me" \
  -H "X-Device-Id: speaker-1" \
  -H "Content-Type: application/json" \
  -d '{"text":"status"}' -o reply.pcm \
  http://127.0.0.1:8090/v1/utterance/text

ffplay -f s16le -ar 24000 -ac 1 reply.pcm
```

## WebSocket (v1.1)

Not implemented. Future duplex should reuse `TurnPipeline` / `AudioSink` and stream mic frames up while Intercom streams PCM down on one connection.
