# Intercom HTTP API

Base URL default: `http://127.0.0.1:8090`  
Auth: `Authorization: Bearer <device_token>` (Intercom device secret — not an Arbiter `atr_` token).

## `GET /health`

Returns JSON readiness for whisper, kokoro, and Arbiter reachability.

- `200` when whisper + kokoro are ready (Arbiter may still be down).
- `503` if speech binaries/models are missing.

`whisper.detail` / `kokoro.detail` say `server http://127.0.0.1:8092` (or `8091`) when the warm daemons are up.

Each turn prints one stderr line:

```
intercom latency turn=… path=arbiter stt_ms=… conv_ms=… arbiter_ttft_ms=… first_sentence_ms=… kokoro_ms=… kokoro_total_ms=… ttfa_ms=… ttfa_kind=ack|answer|fast first_answer_ms=…
```

`ttfa_ms` is time-to-first-audio on the chunked PCM response. `-1` means that stage did not run.

## `POST /v1/utterance`

Push-to-talk audio in → chunked PCM out.

### Request

| Header | Required | Description |
|--------|----------|-------------|
| `Authorization` | yes | `Bearer <device_token>` |
| `X-Device-Id` | yes | Stable device id (session key) |
| `Content-Type` | recommended | `audio/L16; rate=24000; channels=1` |
| `X-Sample-Rate` | no | Overrides rate if Content-Type omitted |

Body: raw mono PCM **s16le** (default 24 kHz).

### Response

`Content-Type: audio/L16; rate=24000; channels=1` (chunked).

| Header | Description |
|--------|-------------|
| `X-Turn-Id` | Intercom turn id (use for cancel) |
| `X-Transcript` | STT text |
| `X-Device-Id` | Echo |
| `X-Conversation-Id` | Present when a prior session exists |
| `X-Fast-Path` | `1` when the turn skipped Arbiter |
| `X-Fast-Path-Kind` | `social`, `clock`, `echo`, `timer`, `light_on`, `light_off`, `light_toggle`, `volume_up`, `volume_down`, `weather`, or `alarm` |
| `X-Intercom-Error` | Error detail when present |

Errors before streaming are JSON (`401`, `400`, `502`).

### Example

```bash
curl -N \
  -H "Authorization: Bearer dev-device-secret-change-me" \
  -H "X-Device-Id: speaker-1" \
  -H "Content-Type: audio/L16; rate=24000; channels=1" \
  --data-binary @utterance.pcm \
  --output reply.pcm \
  -D headers.txt \
  http://127.0.0.1:8090/v1/utterance
```

## `POST /v1/utterance/text`

Same as utterance but skips STT — for bring-your-own transcript / bridge tests.

```json
{ "text": "what time is it" }
```

Fast-path phrases never call Arbiter:

- Social: `hello` / `good morning` and other greetings, thanks, `status`
- Clock: `what time is it`, `what's the date` (local clock, not tools)
- Echo: `echo …`
- Home (only when `home.ha_base_url` and `home.ha_token` are set): timers, lights, volume, weather at home, next alarm. Weather with a place (`in Tokyo`) still goes to Arthur. A timer with no duration answers `How long, sir?` even without Home Assistant.

Social turns greet back and invite a follow-up. Home intents that match but have no Home Assistant config fall through to Arthur (except the bare timer prompt).

## `POST /v1/turns/:turn_id/cancel`

Barge-in: stops TTS and cancels the Arbiter `request_id` when known.

Requires `Authorization` + `X-Device-Id`.

## `GET /v1/devices/:device_id/session`

Debug: `{ device_id, conversation_id, last_turn_id, updated_at }`.

## Arbiter mapping

| Intercom | Arbiter |
|--------|---------|
| First utterance per device | `POST /v1/conversations` (`agent_id` + `agent_def` from config) |
| Boot (`warm_prefix`) | Same create, then a silent `PREFIX WARM` message so a local model can cache Arthur's constitution. Arthur replies `Ready`. |
| Each turn | `POST /v1/conversations/:id/messages` + SSE |
| `message` | STT transcript only (no voice-intercom suffix) |
| body | `{ "message", "channel": "voice", "agent_def" }` — `agent_def` includes a fresh local date/time rule each turn |
| Arthur | `mode: "spoken"`, `intent.mode: "off"` |
| Cancel | `POST /v1/requests/:id/cancel` |
| Idempotency-Key | Intercom `turn_id` |

Default agent: **Arthur** (`config/arthur.agent.json`).

Reply PCM can start before the SSE `done` event: Intercom synthesizes each completed sentence as depth-0 text deltas arrive (Kokoro latency per sentence, not full-turn latency). Remaining text is flushed after `done`; already-spoken sentences are not repeated.

Related short sentences received together are synthesized as one phrase so
prosody carries across them. PCM is faded once at the bridge and followed by
punctuation-aware pauses (short after commas, longer after questions). Fillers
wait for an actual tool call to remain active before speaking and use a short
tool-appropriate acknowledgement.

At 24 kHz, Intercom uses Kokoro's chunked raw-PCM endpoint and forwards each
native phoneme batch as soon as it is generated. Older external Kokoro servers,
and configurations requiring resampling, fall back to the complete-WAV
endpoint.

Arthur's delivery is inferred from each spoken phrase. Greetings and courtesies
are slightly warmer and more measured; tool-wait asides are quieter and briefer;
warnings are firmer and leave a little more space. The adjustments are subtle
speed, gain, and pause changes on top of the selected Kokoro voice.

Kokoro output passes through a stateful speech DSP chain before reaching the
device: a 70 Hz high-pass filter, a gentle 2.6 kHz presence lift, envelope
compression, makeup gain, and a minus-one-decibel limiter. All parameters are
under `kokoro.dsp`; set `enabled` to `false` for a bit-exact bypass.
