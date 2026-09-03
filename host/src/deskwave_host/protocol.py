"""Versioned DeskWave wire protocol validation and message construction."""

from __future__ import annotations

import json
from dataclasses import dataclass
from datetime import datetime
from typing import Any, TypeGuard

PROTOCOL_VERSION = 1
MAX_MESSAGE_BYTES = 16_384
MAX_SEQUENCE = 2_147_483_647


class ProtocolError(ValueError):
    """A malformed or unsupported wire message."""


@dataclass(frozen=True, slots=True)
class IncomingMessage:
    message_type: str
    sequence: int
    payload: dict[str, Any]
    timestamp_ms: int | None


NO_ARGUMENT_COMMANDS = frozenset(
    {
        "play",
        "pause",
        "toggle",
        "previous",
        "next",
        "volume_up",
        "volume_down",
        "mute",
        "shuffle_toggle",
        "refresh",
    }
)


def _is_int(value: object) -> TypeGuard[int]:
    return isinstance(value, int) and not isinstance(value, bool)


def _bounded_text(value: object, name: str, maximum: int = 128) -> str:
    if not isinstance(value, str) or not value or len(value) > maximum:
        raise ProtocolError(f"{name} must be a non-empty string up to {maximum} characters")
    return value


def _validate_control_payload(payload: dict[str, Any]) -> dict[str, Any]:
    command = _bounded_text(payload.get("command"), "payload.command", 32)
    if command in NO_ARGUMENT_COMMANDS:
        return {"command": command}
    if command == "set_volume":
        value = payload.get("value")
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ProtocolError("set_volume requires a numeric payload.value")
        if not 0.0 <= float(value) <= 1.0:
            raise ProtocolError("set_volume payload.value must be between 0 and 1")
        return {"command": command, "value": float(value)}
    if command == "seek":
        offset_ms = payload.get("offset_ms")
        if not _is_int(offset_ms) or not -1_800_000 <= offset_ms <= 1_800_000:
            raise ProtocolError("seek payload.offset_ms must be an integer within 30 minutes")
        return {"command": command, "offset_ms": offset_ms}
    if command == "set_repeat":
        mode = payload.get("mode")
        if mode not in {"off", "track", "playlist"}:
            raise ProtocolError("set_repeat payload.mode must be off, track, or playlist")
        return {"command": command, "mode": mode}
    if command == "select_player":
        player_id = _bounded_text(payload.get("player_id"), "payload.player_id", 255)
        return {"command": command, "player_id": player_id}
    raise ProtocolError(f"unsupported control command: {command}")


def parse_message(raw: str | bytes) -> IncomingMessage:
    encoded = raw.encode("utf-8") if isinstance(raw, str) else raw
    if not encoded or len(encoded) > MAX_MESSAGE_BYTES:
        raise ProtocolError("message size is invalid")
    try:
        document = json.loads(encoded)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ProtocolError("message is not valid UTF-8 JSON") from error
    if not isinstance(document, dict):
        raise ProtocolError("message root must be an object")
    if document.get("protocol") != PROTOCOL_VERSION:
        raise ProtocolError("unsupported protocol version")
    message_type = _bounded_text(document.get("type"), "type", 32)
    sequence = document.get("sequence")
    if not _is_int(sequence) or not 0 <= sequence <= MAX_SEQUENCE:
        raise ProtocolError("sequence must be a non-negative 31-bit integer")
    timestamp = document.get("timestamp_ms")
    if timestamp is not None and (not _is_int(timestamp) or timestamp < 0):
        raise ProtocolError("timestamp_ms must be a non-negative integer")
    payload = document.get("payload")
    if not isinstance(payload, dict):
        raise ProtocolError("payload must be an object")
    if message_type == "control":
        payload = _validate_control_payload(payload)
    elif message_type in {"ping", "device_status", "list_players"}:
        payload = dict(payload)
    else:
        raise ProtocolError(f"unsupported message type: {message_type}")
    return IncomingMessage(message_type, sequence, payload, timestamp)


def make_message(message_type: str, sequence: int, payload: dict[str, Any]) -> dict[str, Any]:
    if not 0 <= sequence <= MAX_SEQUENCE:
        raise ValueError("sequence is outside the protocol range")
    now = datetime.now().astimezone()
    utc_offset = now.utcoffset()
    return {
        "protocol": PROTOCOL_VERSION,
        "type": message_type,
        "sequence": sequence,
        "timestamp_ms": int(now.timestamp() * 1000),
        "utc_offset_seconds": 0 if utc_offset is None else int(utc_offset.total_seconds()),
        "payload": payload,
    }
