import pytest

from deskwave_host.models import (
    LyricLine,
    LyricsStatus,
    MediaKind,
    PlaybackState,
    PlaybackStatus,
    PlayerSummary,
    QueueEntry,
    ThemePalette,
    select_active_player,
)


def player(player_id: str, status: PlaybackStatus) -> PlayerSummary:
    return PlayerSummary(player_id, player_id, status)


def test_active_player_prefers_playing_then_retains_previous() -> None:
    players = [
        player("org.mpris.MediaPlayer2.vlc", PlaybackStatus.PLAYING),
        player("org.mpris.MediaPlayer2.spotify", PlaybackStatus.PLAYING),
    ]
    selected = select_active_player(players, previous_id="org.mpris.MediaPlayer2.vlc")
    assert selected and selected.player_id.endswith("vlc")


def test_active_player_honors_manual_preference_when_playing() -> None:
    players = [player("b", PlaybackStatus.PLAYING), player("a", PlaybackStatus.PLAYING)]
    assert select_active_player(players, preferred_id="b") == players[0]


def test_state_normalization_clamps_untrusted_numeric_fields() -> None:
    state = PlaybackState(duration_ms=1000, position_ms=2000, volume=2.0).normalized()
    assert state.position_ms == 1000
    assert state.volume == 1.0


def test_state_payload_contains_bounded_upcoming_queue() -> None:
    state = PlaybackState(
        queue=tuple(QueueEntry(title=f"Track {index}") for index in range(6)),
        queue_available=True,
    ).normalized()

    payload = state.to_payload()
    assert payload["capabilities"]["queue"] is True
    assert [entry["title"] for entry in payload["queue"]] == [
        "Track 0",
        "Track 1",
        "Track 2",
        "Track 3",
    ]


def test_state_payload_contains_bounded_synchronized_lyric_window() -> None:
    state = PlaybackState(
        lyrics_status=LyricsStatus.SYNCED,
        lyrics=tuple(LyricLine(index * 1_000, f"Line {index}") for index in range(8)),
    ).normalized()

    payload = state.to_payload()

    assert payload["lyrics"]["status"] == "synced"
    assert payload["lyrics"]["lines"] == [
        {"time_ms": index * 1_000, "text": f"Line {index}"} for index in range(5)
    ]


def test_podcast_state_publishes_transcript_instead_of_lyrics() -> None:
    state = PlaybackState(
        media_kind=MediaKind.PODCAST,
        lyrics_status=LyricsStatus.SYNCED,
        lyrics=(LyricLine(0, "Caption one"),),
    )

    payload = state.to_payload()

    assert "lyrics" not in payload
    assert payload["transcript"] == {
        "status": "synced",
        "lines": [{"time_ms": 0, "text": "Caption one"}],
    }


def test_state_payload_contains_strict_theme_and_generation() -> None:
    theme = ThemePalette(
        primary=0x123456,
        secondary=0x654321,
        background=0x010203,
        foreground=0xFAFBFC,
    )
    state = PlaybackState(theme=theme, artwork_generation=17)

    payload = state.to_payload()

    assert payload["theme"] == {
        "primary": 0x123456,
        "secondary": 0x654321,
        "background": 0x010203,
        "foreground": 0xFAFBFC,
    }
    assert payload["artwork_generation"] == 17
    assert PlaybackState().to_payload()["theme"] is None


@pytest.mark.parametrize("value", [-1, 0x1000000, 1.5, True])
def test_theme_palette_rejects_non_rgb_values(value: object) -> None:
    with pytest.raises(ValueError, match="packed 24-bit RGB"):
        ThemePalette(
            primary=value,  # type: ignore[arg-type]
            secondary=0,
            background=0,
            foreground=0,
        )


def test_theme_and_generation_participate_in_content_identity() -> None:
    first = PlaybackState(
        theme=ThemePalette(0x112233, 0x445566, 0x010203, 0xF0F0F0),
        artwork_generation=1,
    )
    second = PlaybackState(
        theme=ThemePalette(0x112234, 0x445566, 0x010203, 0xF0F0F0),
        artwork_generation=2,
    )

    assert first.content_key() != second.content_key()


@pytest.mark.parametrize("generation", [-1, 0x1_0000_0000, True])
def test_artwork_generation_is_strict_uint32(generation: object) -> None:
    with pytest.raises(ValueError, match="unsigned 32-bit"):
        PlaybackState(artwork_generation=generation)  # type: ignore[arg-type]
