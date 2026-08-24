import json

import pytest

from deskwave_host.api.server import (
    MAX_DEVICE_MESSAGE_BYTES,
    MAX_DEVICE_TEXT_BYTES,
    RateLimiter,
    _encode_device_message,
    _players_payload,
    _state_payload,
)
from deskwave_host.models import (
    PlaybackState,
    PlaybackStatus,
    PlayerSummary,
    QueueEntry,
    ThemePalette,
)
from deskwave_host.protocol import make_message


def test_rate_limiter_bounds_tracked_identities() -> None:
    limiter = RateLimiter(maximum_identities=3)
    for index in range(10):
        assert limiter.allow(f"pair-request:192.0.2.{index}")
    assert len(limiter._attempts) == 3


def test_state_payload_keeps_artwork_theme_and_generation_together() -> None:
    artwork_id = "a" * 64
    theme = ThemePalette(0x123456, 0x654321, 0x010203, 0xFAFBFC)

    payload = _state_payload(
        PlaybackState(
            artwork_id=artwork_id,
            theme=theme,
            artwork_generation=9,
        )
    )

    assert payload["artwork_id"] == artwork_id
    assert payload["artwork_path"] == f"/v1/artwork/{artwork_id}.jpg"
    assert payload["theme"] == theme.to_payload()
    assert payload["artwork_generation"] == 9


@pytest.mark.parametrize(
    "character",
    ['"', "\\", "\x01", "\x85", '"\\\x01', "é", "夜", "🎵"],
)
def test_maximum_production_state_with_theme_fits_firmware_frame_limit(
    character: str,
) -> None:
    artwork_id = "f" * 64
    state = PlaybackState(
        title=character * 256,
        artists=tuple(character * 160 for _ in range(8)),
        album=character * 256,
        artwork_id=artwork_id,
        theme=ThemePalette(0xFFFFFF, 0xFFFFFE, 0x010101, 0xF0F0F0),
        artwork_generation=0xFFFF_FFFF,
        player_id=character * 255,
        player_name=character * 128,
        track_id=character * 512,
        queue=tuple(
            QueueEntry(
                title=character * 256,
                artist=character * 160,
                track_id=character * 512,
            )
            for _ in range(4)
        ),
        queue_available=True,
    )
    message = make_message("playback_state", 1, _state_payload(state))

    encoded = _encode_device_message(message).encode("utf-8")
    decoded = json.loads(encoded)

    assert len(encoded) <= MAX_DEVICE_MESSAGE_BYTES
    assert len(decoded["payload"]["title"].encode("utf-8")) <= MAX_DEVICE_TEXT_BYTES
    assert all(
        ord(value) >= 0x20 and not 0x7F <= ord(value) <= 0x9F
        for value in decoded["payload"]["title"]
    )
    assert len(", ".join(decoded["payload"]["artists"]).encode("utf-8")) <= MAX_DEVICE_TEXT_BYTES
    assert all(
        len(value.encode("utf-8")) <= MAX_DEVICE_TEXT_BYTES
        for entry in decoded["payload"]["queue"]
        for value in (entry["title"], entry["artist"], entry["track_id"])
    )


def test_device_encoder_rejects_an_unbounded_programming_error() -> None:
    message = make_message("pong", 1, {"nonce": "x" * MAX_DEVICE_MESSAGE_BYTES})

    with pytest.raises(ValueError, match="firmware receive limit"):
        _encode_device_message(message)


def test_maximum_player_list_fits_firmware_frame_limit() -> None:
    hostile_text = '"\\\x01🎵' * 255
    players = [PlayerSummary(hostile_text, hostile_text, PlaybackStatus.PAUSED) for _ in range(6)]
    message = make_message(
        "players",
        1,
        {"request_sequence": 1, "players": _players_payload(players)},
    )

    encoded = _encode_device_message(message).encode("utf-8")

    assert len(encoded) <= MAX_DEVICE_MESSAGE_BYTES
