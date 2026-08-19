# Intercom HTTP API

Base URL default: `http://127.0.0.1:8090`  
Auth: `Authorization: Bearer <device_token>` (Intercom device secret — not an Arbiter `atr_` token).

## `GET /health`

Returns JSON readiness for whisper, kokoro, and Arbiter reachability.

- `200` when whisper + kokoro are ready (Arbiter may still be down).
- `503` if speech binaries/models are missing.

## `POST /v1/utterance`

Push-to-talk audio in → chunked PCM out.

### Request

| Header | Required | Description |
|--------|----------|-------------|
| `Authorization` | yes | `Bearer <device_token>` |
| `X-Device-Id` | yes | Stable device id (session key) |
| `Content-Type` | recommended | `audio/L16; rate=16000; channels=1` |
| `X-Sample-Rate` | no | Overrides rate if Content-Type omitted |

Body: raw mono PCM **s16le** (default 16 kHz).

### Response

`Content-Type: audio/L16; rate=16000; channels=1` (chunked).

| Header | Description |
|--------|-------------|
| `X-Turn-Id` | Intercom turn id (use for cancel) |
| `X-Transcript` | STT text |
| `X-Device-Id` | Echo |
| `X-Conversation-Id` | Present when a prior session exists |
| `X-Intercom-Error` | Error detail when present |

Errors before streaming are JSON (`401`, `400`, `502`).

### Example

```bash
curl -N \
  -H "Authorization: Bearer dev-device-secret-change-me" \
  -H "X-Device-Id: speaker-1" \
  -H "Content-Type: audio/L16; rate=16000; channels=1" \
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

Fast-path phrases (`ping`, `status`, `what time…`, `echo …`) never call Arbiter.

## `POST /v1/turns/:turn_id/cancel`

Barge-in: stops TTS and cancels the Arbiter `request_id` when known.

Requires `Authorization` + `X-Device-Id`.

## `GET /v1/devices/:device_id/session`

Debug: `{ device_id, conversation_id, last_turn_id, updated_at }`.

## Arbiter mapping

| Intercom | Arbiter |
|--------|---------|
| First utterance per device | `POST /v1/conversations` (`agent_id` + `agent_def` from config) |
| Each turn | `POST /v1/conversations/:id/messages` + SSE |
| Cancel | `POST /v1/requests/:id/cancel` |
| Idempotency-Key | Intercom `turn_id` |

Default agent: **Arthur** (`config/arthur.agent.json`).
