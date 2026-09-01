from __future__ import annotations

import json

import pytest

from deskwave_host.protocol import ProtocolError, make_message, parse_message


def wire(message_type: str, payload: dict[str, object], sequence: int = 7) -> str:
    return json.dumps(
        {
            "protocol": 1,
            "type": message_type,
            "sequence": sequence,
            "timestamp_ms": 123,
            "payload": payload,
        }
    )


@pytest.mark.parametrize(
    ("payload", "expected"),
    [
        ({"command": "toggle", "ignored": True}, {"command": "toggle"}),
        ({"command": "set_volume", "value": 0.75}, {"command": "set_volume", "value": 0.75}),
        ({"command": "seek", "offset_ms": -15_000}, {"command": "seek", "offset_ms": -15_000}),
        ({"command": "set_repeat", "mode": "track"}, {"command": "set_repeat", "mode": "track"}),
    ],
)
def test_valid_control_messages(payload: dict[str, object], expected: dict[str, object]) -> None:
    assert parse_message(wire("control", payload)).payload == expected


@pytest.mark.parametrize(
    "raw",
    [
        "",
        "[]",
        "not-json",
        wire("control", {"command": "set_volume", "value": 2}),
        wire("control", {"command": "seek", "offset_ms": True}),
        wire("control", {"command": "unknown"}),
        json.dumps({"protocol": 2, "type": "ping", "sequence": 1, "payload": {}}),
        json.dumps({"protocol": 1, "type": "ping", "sequence": True, "payload": {}}),
    ],
)
def test_malformed_messages_are_rejected(raw: str) -> None:
    with pytest.raises(ProtocolError):
        parse_message(raw)


def test_message_builder_sets_protocol_envelope() -> None:
    message = make_message("hello", 4, {"ready": True})
    assert message["protocol"] == 1
    assert message["type"] == "hello"
    assert message["sequence"] == 4
    assert isinstance(message["timestamp_ms"], int)
    assert isinstance(message["utc_offset_seconds"], int)
    assert -24 * 60 * 60 <= message["utc_offset_seconds"] <= 24 * 60 * 60
