#!/usr/bin/env python3
"""Keep Kokoro ONNX loaded and serve WAV over HTTP.

POST /v1/tts  {"text": "..."}  -> audio/wav
GET  /health                   -> {"ok": true}
"""

from __future__ import annotations

import argparse
import io
import json
import sys
import threading
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import numpy as np
from kokoro_onnx import Kokoro

ENGINE = None
LOCK = threading.Lock()
VOICE = "af_heart"
SPEED = 1.0
LANG = "en-us"


def voice_lang(voice: str, override: str | None) -> str:
    if override:
        return override
    prefix = voice[:3] if len(voice) >= 3 else ""
    return {
        "bf_": "en-gb",
        "bm_": "en-gb",
        "ff_": "fr-fr",
        "if_": "it",
        "im_": "it",
        "jf_": "ja",
        "jm_": "ja",
        "zf_": "cmn",
        "zm_": "cmn",
    }.get(prefix, "en-us")


def synthesize(text: str) -> tuple[bytes, int]:
    if ENGINE is None:
        raise RuntimeError("engine not loaded")
    with LOCK:
        audio, sr = ENGINE.create(text, voice=VOICE, speed=SPEED, lang=LANG)
    pcm = np.clip(np.asarray(audio) * 32767.0, -32768, 32767).astype(np.int16)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(int(sr))
        wav.writeframes(pcm.tobytes())
    return buf.getvalue(), int(sr)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def _send(self, status: int, body: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802
        if self.path.split("?", 1)[0] != "/health":
            self._send(404, b'{"error":"not found"}', "application/json")
            return
        body = json.dumps({"ok": True, "voice": VOICE, "lang": LANG}).encode("utf-8")
        self._send(200, body, "application/json")

    def do_POST(self) -> None:  # noqa: N802
        if self.path.split("?", 1)[0] != "/v1/tts":
            self._send(404, b'{"error":"not found"}', "application/json")
            return
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length <= 0 or length > 32 * 1024:
            self._send(400, b'{"error":"invalid body"}', "application/json")
            return
        raw = self.rfile.read(length)
        try:
            payload = json.loads(raw.decode("utf-8"))
            text = (payload.get("text") or "").strip()
        except (json.JSONDecodeError, UnicodeDecodeError, AttributeError):
            self._send(400, b'{"error":"invalid json"}', "application/json")
            return
        if not text:
            self._send(400, b'{"error":"missing text"}', "application/json")
            return
        try:
            wav, _sr = synthesize(text)
        except Exception as exc:  # noqa: BLE001
            msg = json.dumps({"error": str(exc)}).encode("utf-8")
            self._send(500, msg, "application/json")
            return
        self._send(200, wav, "audio/wav")


def main() -> int:
    global ENGINE, VOICE, SPEED, LANG

    parser = argparse.ArgumentParser(description="Warm Kokoro ONNX HTTP server")
    parser.add_argument("--model", required=True)
    parser.add_argument("--voices", required=True)
    parser.add_argument("--voice", default="af_heart")
    parser.add_argument("--speed", type=float, default=1.0)
    parser.add_argument("--lang", default="")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8091)
    args = parser.parse_args()

    VOICE = args.voice
    SPEED = args.speed
    LANG = voice_lang(args.voice, args.lang or None)

    print(f"intercom kokoro-server: loading {args.model}", flush=True)
    ENGINE = Kokoro(args.model, args.voices)
    print(
        f"intercom kokoro-server: listening on http://{args.host}:{args.port} "
        f"voice={VOICE} lang={LANG}",
        flush=True,
    )
    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
