#!/usr/bin/env python3
"""Exercise a built UI provider against the complete `aura.ui.v1` stdio flow."""

from __future__ import annotations

import argparse
import queue
import struct
import subprocess
import sys
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Any

MAX_FRAME = 16 * 1024 * 1024
PROTOCOL = "aura.ui.v1"


@dataclass(frozen=True)
class Cursor:
    data: bytes
    offset: int = 0


class ProtocolError(RuntimeError):
    """Raised when a provider violates the frozen wire or session contract."""


def take(cursor: Cursor, count: int) -> tuple[bytes, Cursor]:
    end = cursor.offset + count
    if end > len(cursor.data):
        raise ProtocolError("frame body is truncated")
    return cursor.data[cursor.offset:end], Cursor(cursor.data, end)


def byte(cursor: Cursor, expected: int | None = None) -> tuple[int, Cursor]:
    raw, following = take(cursor, 1)
    value = raw[0]
    if expected is not None and value != expected:
        raise ProtocolError(f"expected byte 0x{expected:02x}, got 0x{value:02x}")
    return value, following


def u32(cursor: Cursor) -> tuple[int, Cursor]:
    raw, following = take(cursor, 4)
    return struct.unpack(">I", raw)[0], following


def i64(cursor: Cursor) -> tuple[int, Cursor]:
    raw, following = take(cursor, 8)
    return struct.unpack(">q", raw)[0], following


def string(cursor: Cursor) -> tuple[str, Cursor]:
    marker, after_marker = byte(cursor, 0xDB)
    del marker
    length, after_length = u32(after_marker)
    raw, after_value = take(after_length, length)
    try:
        return raw.decode("utf-8"), after_value
    except UnicodeDecodeError as failure:
        raise ProtocolError("string is not UTF-8") from failure


def write_string(out: bytearray, text: str) -> None:
    encoded = text.encode("utf-8")
    if len(encoded) > 0xFFFFFFFF:
        raise ProtocolError("string exceeds the wire limit")
    out.append(0xDB)
    out.extend(struct.pack(">I", len(encoded)))
    out.extend(encoded)


def encode_value(value: Any, depth: int = 0) -> bytes:
    if depth > 63:
        raise ProtocolError("value is too deeply nested")
    out = bytearray((0x92,))
    if value is None:
        out.extend((0x00, 0xC0))
    elif value is True:
        out.extend((0x01, 0xC3))
    elif value is False:
        out.extend((0x01, 0xC2))
    elif isinstance(value, int):
        out.append(0x02)
        out.append(0xD3)
        out.extend(struct.pack(">q", value))
    elif isinstance(value, float):
        out.extend((0x03, 0xCB))
        out.extend(struct.pack(">d", value))
    elif isinstance(value, str):
        out.append(0x04)
        write_string(out, value)
    elif isinstance(value, (bytes, bytearray)):
        payload = bytes(value)
        if len(payload) > 0xFFFFFFFF:
            raise ProtocolError("bytes exceed the wire limit")
        out.extend((0x05, 0xC6))
        out.extend(struct.pack(">I", len(payload)))
        out.extend(payload)
    elif isinstance(value, list):
        out.append(0x06)
        out.append(0xDD)
        out.extend(struct.pack(">I", len(value)))
        for item in value:
            out.extend(encode_value(item, depth + 1))
    elif isinstance(value, dict):
        entries = list(value.items())
        out.append(0x07)
        out.append(0xDD)
        out.extend(struct.pack(">I", len(entries)))
        for key, item in entries:
            out.append(0x92)
            write_string(out, key)
            out.extend(encode_value(item, depth + 1))
    else:
        raise ProtocolError(f"unsupported smoke value: {type(value).__name__}")
    return bytes(out)


def decode_value(cursor: Cursor, depth: int = 0) -> tuple[Any, Cursor]:
    _, cursor = byte(cursor, 0x92)
    tag, cursor = byte(cursor)
    if tag == 0:
        _, cursor = byte(cursor, 0xC0)
        return None, cursor
    if tag == 1:
        encoded, cursor = byte(cursor)
        if encoded not in (0xC2, 0xC3):
            raise ProtocolError("boolean is not canonical")
        return encoded == 0xC3, cursor
    if tag == 2:
        _, cursor = byte(cursor, 0xD3)
        return i64(cursor)
    if tag == 3:
        _, cursor = byte(cursor, 0xCB)
        number, cursor = i64(cursor)
        return struct.unpack(">d", struct.pack(">q", number))[0], cursor
    if tag == 4:
        return string(cursor)
    if tag == 5:
        _, cursor = byte(cursor, 0xC6)
        length, cursor = u32(cursor)
        raw, cursor = take(cursor, length)
        return raw, cursor
    if tag == 6:
        marker, cursor = byte(cursor, 0xDD)
        del marker
        count, cursor = u32(cursor)
        values: list[Any] = []
        for _ in range(count):
            item, cursor = decode_value(cursor, depth + 1)
            values.append(item)
        return values, cursor
    if tag == 7:
        marker, cursor = byte(cursor, 0xDD)
        del marker
        count, cursor = u32(cursor)
        result: dict[str, Any] = {}
        for _ in range(count):
            _, cursor = byte(cursor, 0x92)
            key, cursor = string(cursor)
            if key in result:
                raise ProtocolError(f"duplicate map key: {key}")
            item, cursor = decode_value(cursor, depth + 1)
            result[key] = item
        return result, cursor
    raise ProtocolError(f"unsupported value tag: {tag}")


def encode_envelope(kind: str, request_id: int, **fields: Any) -> bytes:
    value: dict[str, Any] = {
        "schemaVersion": 1,
        "type": kind,
        "requestId": request_id,
    }
    value.update(fields)
    return encode_value(value)


def decode_envelope(body: bytes) -> dict[str, Any]:
    value, trailing = decode_value(Cursor(body))
    if trailing.offset != len(body):
        raise ProtocolError("trailing bytes after envelope")
    if not isinstance(value, dict):
        raise ProtocolError("envelope is not a map")
    return value


def read_frame(stream: Any) -> bytes | None:
    header = stream.read(4)
    if not header:
        return None
    if len(header) != 4:
        raise ProtocolError("frame header is truncated")
    length = struct.unpack(">I", header)[0]
    if length == 0 or length > MAX_FRAME:
        raise ProtocolError("frame length is outside bounds")
    body = stream.read(length)
    if len(body) != length:
        raise ProtocolError("frame body is truncated")
    return body


class Reader:
    """Reads provider frames on a daemon thread so pipes cannot deadlock."""

    def __init__(self, stream: Any) -> None:
        self.queue: queue.Queue[dict[str, Any] | None] = queue.Queue()
        self.failure: BaseException | None = None
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.stream = stream
        self.thread.start()

    def _run(self) -> None:
        try:
            while True:
                body = read_frame(self.stream)
                if body is None:
                    self.queue.put(None)
                    return
                self.queue.put(decode_envelope(body))
        except BaseException as failure:  # Preserve the worker failure for the main thread.
            self.failure = failure
            self.queue.put(None)

    def next_message(self, timeout: float) -> dict[str, Any]:
        try:
            message = self.queue.get(timeout=timeout)
        except queue.Empty as failure:
            raise ProtocolError(f"timed out after {timeout:g}s waiting for a provider frame") from failure
        if message is None:
            if self.failure is not None:
                raise ProtocolError(f"reader failed: {self.failure}") from self.failure
            raise ProtocolError("provider closed stdout before shutdown")
        return message


def require_fields(message: dict[str, Any], expected: set[str]) -> None:
    if set(message) != expected:
        raise ProtocolError(f"wrong envelope fields: {sorted(message)}")


def snapshot() -> dict[str, Any]:
    instance = {
        "id": "smoke-instance",
        "name": "Smoke Instance",
        "version": "1.21.9",
        "loader": "Vanilla",
        "lastPlayed": "2026-09-06 07:00",
        "playTime": "0.1 小时",
        "modCount": 1,
        "description": "stdio smoke fixture",
        "isFavorite": True,
    }
    account = {"id": "smoke-account", "username": "aura-smoke", "type": "offline"}
    return {
        "instances": [instance],
        "accounts": [account],
        "settings": {"uiFrontend": "dev.aura.modern-ui", "downloadThreads": 16},
        "pluginContributions": [],
    }


class Session:
    """Owns the provider process and one canonical frame transport."""

    def __init__(self, binary: Path, timeout: float) -> None:
        self.process = subprocess.Popen(
            [str(binary), "--stdio"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert self.process.stdin is not None
        assert self.process.stdout is not None
        self.input = self.process.stdin
        self.reader = Reader(self.process.stdout)
        self.diagnostics: list[str] = []
        self.timeout = timeout
        assert self.process.stderr is not None
        threading.Thread(target=self._drain_stderr, args=(self.process.stderr,), daemon=True).start()

    def _drain_stderr(self, stream: Any) -> None:
        for line in iter(stream.readline, b""):
            if len(self.diagnostics) < 100:
                self.diagnostics.append(line.decode("utf-8", "replace").rstrip())

    def send(self, body: bytes) -> None:
        if not body or len(body) > MAX_FRAME:
            raise ProtocolError("outgoing frame length is outside bounds")
        self.input.write(struct.pack(">I", len(body)))
        self.input.write(body)
        self.input.flush()

    def request(self, request_id: int, method: str, params: Any) -> None:
        self.send(encode_envelope("request", request_id, method=method, params=params))

    def result(self, request_id: int, value: Any) -> None:
        self.send(encode_envelope("result", request_id, value=value))

    def expect_result(self, request_id: int) -> Any:
        while True:
            message = self.reader.next_message(self.timeout)
            if message.get("type") == "result" and message.get("requestId") == request_id:
                require_fields(message, {"schemaVersion", "type", "requestId", "value"})
                return message["value"]
            self.handle_unexpected(message, f"result {request_id}")

    def expect_request(self, request_id: int, method: str) -> Any:
        while True:
            message = self.reader.next_message(self.timeout)
            if message.get("type") == "request" and message.get("requestId") == request_id:
                require_fields(message, {"schemaVersion", "type", "requestId", "method", "params"})
                if message["method"] != method:
                    raise ProtocolError(f"expected {method}, got {message['method']}")
                if request_id % 2 != 0:
                    raise ProtocolError("frontend request identifier is not even")
                return message["params"]
            self.handle_unexpected(message, f"request {request_id} {method}")

    def handle_unexpected(self, message: dict[str, Any], waiting_for: str) -> None:
        if message.get("type") == "request":
            request_id = message.get("requestId", 0)
            if isinstance(request_id, int) and request_id > 0 and request_id % 2 == 0:
                self.result(request_id, None)
                return
        raise ProtocolError(f"unexpected message while waiting for {waiting_for}: {message}")

    def close(self) -> int:
        if self.process.stdin is not None:
            self.process.stdin.close()
        try:
            return self.process.wait(timeout=self.timeout)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
            raise ProtocolError("provider did not exit after ui.shutdown")


def run(binary: Path, timeout: float) -> None:
    session = Session(binary, timeout)
    try:
        run_session(session)
    except BaseException:
        if session.process.poll() is None:
            session.process.kill()
            session.process.wait(timeout=5)
        raise


def run_session(session: Session) -> None:
    hello = {"protocol": PROTOCOL, "abi": 1}
    session.request(1, "ui.hello", hello)
    answered = session.expect_result(1)
    if answered != hello:
        raise ProtocolError(f"hello mismatch: {answered!r}")

    session.request(3, "ui.snapshot.replace", snapshot())
    if session.expect_result(3) is not None:
        raise ProtocolError("snapshot replace did not return null")

    params = session.expect_request(2, "ui.ready")
    if params is not None:
        raise ProtocolError(f"ui.ready has parameters: {params!r}")
    session.result(2, None)

    params = session.expect_request(4, "core.snapshot.get")
    if params is not None:
        raise ProtocolError(f"snapshot request has parameters: {params!r}")
    session.result(4, snapshot())

    session.request(5, "ui.navigate", {"route": "instances"})
    if session.expect_result(5) is None:
        raise ProtocolError("navigate result should mirror its route")
    session.request(7, "ui.notify", {"title": "Smoke", "message": "stdio verified"})
    if session.expect_result(7) is not None:
        raise ProtocolError("notify did not return null")

    session.request(9, "ui.shutdown", None)
    if session.expect_result(9) is not None:
        raise ProtocolError("shutdown did not return null")
    exit_code = session.close()
    if exit_code != 0:
        raise ProtocolError(f"provider exited with {exit_code}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=30.0)
    arguments = parser.parse_args()
    if not arguments.binary.is_file():
        print(f"missing provider binary: {arguments.binary}", file=sys.stderr)
        return 2
    try:
        run(arguments.binary, arguments.timeout)
    except BaseException as failure:
        print(f"stdio smoke failed: {failure}", file=sys.stderr)
        return 1
    print(f"stdio smoke passed: {arguments.binary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
