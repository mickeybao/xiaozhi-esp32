#!/usr/bin/env python3
"""Small HTTP music proxy for XiaoZhi.

It streams an input audio URL through ffmpeg as Ogg Opus so the existing
firmware radio player can consume it.
"""

from __future__ import annotations

import argparse
import json
import mimetypes
import shutil
import signal
import socket
import subprocess
import sys
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, quote, unquote, urlparse


DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8765
DEFAULT_SAMPLE_RATE = 16000
DEFAULT_BITRATE = "24k"
DEFAULT_CHANNELS = 1
DEFAULT_FRAME_DURATION_MS = 60
DEFAULT_FILES_DIR = Path(__file__).with_name("files")


def guess_lan_ip() -> str:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))
        return sock.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        sock.close()


def load_catalog(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, list):
        raise ValueError(f"catalog must be a JSON array: {path}")
    return [item for item in data if isinstance(item, dict)]


def is_http_url(value: str) -> bool:
    parsed = urlparse(value)
    return parsed.scheme in {"http", "https"} and bool(parsed.netloc)


def extract_stream_url(raw_query: str, query: dict[str, list[str]]) -> str:
    if raw_query.startswith("url="):
        return unquote(raw_query[len("url="):])

    marker = "&url="
    marker_index = raw_query.find(marker)
    if marker_index >= 0:
        return unquote(raw_query[marker_index + len(marker):])

    values = query.get("url", [])
    return unquote(values[0]) if values else ""


def normalize_executable_path(value: str | None) -> str | None:
    if not value:
        return None
    return str(Path(value).expanduser().resolve())


def check_ffmpeg(ffmpeg: str | None) -> dict[str, Any]:
    if not ffmpeg:
        return {
            "ok": False,
            "error": "ffmpeg not found",
        }
    try:
        proc = subprocess.run(
            [ffmpeg, "-version"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=5,
            check=False,
        )
    except Exception as exc:
        return {
            "ok": False,
            "error": str(exc),
        }

    stdout = proc.stdout.decode("utf-8", errors="replace").splitlines()
    stderr = proc.stderr.decode("utf-8", errors="replace").strip()
    return {
        "ok": proc.returncode == 0,
        "returncode": proc.returncode,
        "version": stdout[0] if stdout else "",
        "error": stderr,
    }


def safe_file_path(files_dir: Path, filename: str) -> Path | None:
    if not filename or filename != Path(filename).name:
        return None
    if filename in {".", ".."}:
        return None

    root = files_dir.resolve()
    candidate = (root / filename).resolve()
    try:
        candidate.relative_to(root)
    except ValueError:
        return None
    return candidate


class MusicProxyHandler(BaseHTTPRequestHandler):
    server_version = "XiaoZhiMusicProxy/0.1"

    def log_message(self, fmt: str, *args: Any) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    @property
    def app(self) -> "MusicProxyServer":
        return self.server  # type: ignore[return-value]

    def do_GET(self) -> None:
        request_path, raw_query = self.split_request_target(self.path)
        query = parse_qs(raw_query)

        try:
            if request_path == "/health":
                self.send_json({
                    "ok": True,
                    "ffmpeg": self.app.ffmpeg,
                    "ffmpeg_check": check_ffmpeg(self.app.ffmpeg),
                })
            elif request_path == "/download":
                self.handle_download(query)
            elif request_path.startswith("/download/"):
                self.handle_download_file(unquote(request_path.removeprefix("/download/")))
            elif request_path == "/stream":
                self.handle_stream(raw_query, query)
            elif request_path == "/debug/stream-url":
                self.send_json({"source_url": extract_stream_url(raw_query, query)})
            elif request_path == "/music/search":
                self.handle_music_search(query)
            elif request_path == "/music/play":
                self.handle_music_play(query)
            else:
                self.send_error_json(HTTPStatus.NOT_FOUND, "unknown endpoint")
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        except Exception as exc:
            self.send_error_json(HTTPStatus.INTERNAL_SERVER_ERROR, str(exc))

    @staticmethod
    def split_request_target(target: str) -> tuple[str, str]:
        if target.startswith("http://") or target.startswith("https://"):
            parsed = urlparse(target)
            return parsed.path, parsed.query
        if "?" not in target:
            return target, ""
        path, raw_query = target.split("?", 1)
        return path, raw_query

    def handle_download(self, query: dict[str, list[str]]) -> None:
        filename = self.first_query_value(query, "file")
        self.handle_download_file(filename)

    def handle_download_file(self, filename: str) -> None:
        file_path = safe_file_path(self.app.files_dir, filename)
        if file_path is None:
            self.send_error_json(HTTPStatus.BAD_REQUEST, "invalid file")
            return
        if not file_path.is_file():
            self.send_error_json(HTTPStatus.NOT_FOUND, "file not found")
            return

        content_type = mimetypes.guess_type(file_path.name)[0] or "application/octet-stream"
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(file_path.stat().st_size))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()

        with file_path.open("rb") as f:
            while True:
                chunk = f.read(64 * 1024)
                if not chunk:
                    break
                self.wfile.write(chunk)

    def handle_stream(self, raw_query: str, query: dict[str, list[str]]) -> None:
        filename = self.first_query_value(query, "file")
        if filename:
            file_path = safe_file_path(self.app.files_dir, filename)
            if file_path is None:
                self.send_error_json(HTTPStatus.BAD_REQUEST, "invalid file")
                return
            if not file_path.is_file():
                self.send_error_json(HTTPStatus.NOT_FOUND, "file not found")
                return
            self.stream_transcoded(str(file_path))
            return

        source_url = extract_stream_url(raw_query, query)
        if not source_url:
            self.send_error_json(HTTPStatus.BAD_REQUEST, "missing url")
            return
        if not is_http_url(source_url):
            self.send_error_json(HTTPStatus.BAD_REQUEST, "url must start with http:// or https://")
            return
        self.stream_transcoded(source_url)

    def handle_music_search(self, query: dict[str, list[str]]) -> None:
        keyword = self.first_query_value(query, "q").strip().lower()
        limit_text = self.first_query_value(query, "limit") or "8"
        try:
            limit = max(1, min(20, int(limit_text)))
        except ValueError:
            limit = 8

        matches = []
        for item in self.app.catalog:
            text = " ".join(str(item.get(k, "")) for k in ("name", "artist", "album")).lower()
            if not keyword or keyword in text:
                matches.append(self.public_song_item(item))
            if len(matches) >= limit:
                break
        self.send_json({"items": matches})

    def handle_music_play(self, query: dict[str, list[str]]) -> None:
        song_id = self.first_query_value(query, "id")
        if not song_id:
            self.send_error_json(HTTPStatus.BAD_REQUEST, "missing id")
            return

        for item in self.app.catalog:
            if str(item.get("id", "")) == song_id:
                source_url = str(item.get("source_url", ""))
                if not is_http_url(source_url):
                    self.send_error_json(HTTPStatus.BAD_REQUEST, "catalog item has no playable source_url")
                    return
                self.stream_transcoded(source_url)
                return
        self.send_error_json(HTTPStatus.NOT_FOUND, "song id not found")

    def stream_transcoded(self, source: str) -> None:
        if not self.app.ffmpeg:
            self.send_error_json(
                HTTPStatus.SERVICE_UNAVAILABLE,
                "ffmpeg not found. Install ffmpeg and make sure it is in PATH.",
            )
            return

        args = [
            self.app.ffmpeg,
            "-hide_banner",
            "-loglevel",
            "error",
            "-nostdin",
            "-i",
            source,
            "-vn",
            "-c:a",
            "libopus",
            "-b:a",
            self.app.bitrate,
            "-ac",
            str(self.app.channels),
            "-ar",
            str(self.app.sample_rate),
            "-frame_duration",
            str(self.app.frame_duration_ms),
            "-f",
            "ogg",
            "pipe:1",
        ]

        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "audio/ogg; codecs=opus")
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Accel-Buffering", "no")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.flush()

        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        try:
            assert proc.stdout is not None
            while True:
                chunk = proc.stdout.read(4096)
                if not chunk:
                    break
                try:
                    self.wfile.write(chunk)
                    self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    break
        finally:
            if proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
                try:
                    proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill()
            if proc.stderr is not None:
                err = proc.stderr.read().decode("utf-8", errors="replace").strip()
                if err:
                    sys.stderr.write(f"ffmpeg: {err}\n")

    def send_json(self, payload: dict[str, Any], status: HTTPStatus = HTTPStatus.OK) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_error_json(self, status: HTTPStatus, message: str) -> None:
        self.send_json({"ok": False, "error": message}, status)

    @staticmethod
    def first_query_value(query: dict[str, list[str]], name: str) -> str:
        values = query.get(name, [])
        return values[0] if values else ""

    @staticmethod
    def public_song_item(item: dict[str, Any]) -> dict[str, str]:
        song_id = str(item.get("id", ""))
        play_url = f"/music/play?id={quote(song_id)}" if song_id else ""
        return {
            "id": song_id,
            "name": str(item.get("name", "")),
            "artist": str(item.get("artist", "")),
            "album": str(item.get("album", "")),
            "play_url": play_url,
        }


class MusicProxyServer(ThreadingHTTPServer):
    def __init__(
        self,
        server_address: tuple[str, int],
        handler_class: type[BaseHTTPRequestHandler],
        *,
        ffmpeg: str | None,
        catalog: list[dict[str, Any]],
        files_dir: Path,
        sample_rate: int,
        bitrate: str,
        channels: int,
        frame_duration_ms: int,
    ) -> None:
        super().__init__(server_address, handler_class)
        self.ffmpeg = ffmpeg
        self.catalog = catalog
        self.files_dir = files_dir
        self.sample_rate = sample_rate
        self.bitrate = bitrate
        self.channels = channels
        self.frame_duration_ms = frame_duration_ms


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="XiaoZhi music transcoding proxy")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    parser.add_argument("--catalog", type=Path, default=Path(__file__).with_name("catalog.json"))
    parser.add_argument("--files-dir", type=Path, default=DEFAULT_FILES_DIR)
    parser.add_argument("--sample-rate", type=int, default=DEFAULT_SAMPLE_RATE)
    parser.add_argument("--bitrate", default=DEFAULT_BITRATE)
    parser.add_argument("--channels", type=int, default=DEFAULT_CHANNELS)
    parser.add_argument("--frame-duration-ms", type=int, default=DEFAULT_FRAME_DURATION_MS)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.ffmpeg = normalize_executable_path(args.ffmpeg)
    catalog = load_catalog(args.catalog)
    files_dir = args.files_dir.resolve()
    files_dir.mkdir(parents=True, exist_ok=True)
    server = MusicProxyServer(
        (args.host, args.port),
        MusicProxyHandler,
        ffmpeg=args.ffmpeg,
        catalog=catalog,
        files_dir=files_dir,
        sample_rate=args.sample_rate,
        bitrate=args.bitrate,
        channels=args.channels,
        frame_duration_ms=args.frame_duration_ms,
    )

    lan_ip = guess_lan_ip()
    print(f"Listening on http://{args.host}:{args.port}")
    print(f"LAN URL: http://{lan_ip}:{args.port}")
    print(f"Health: http://{lan_ip}:{args.port}/health")
    print(f"Files dir: {files_dir}")
    print(f"Catalog items: {len(catalog)}")
    if not args.ffmpeg:
        print("Warning: ffmpeg was not found in PATH. /stream and /music/play will return 503.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
