from deskwave_host.models import (
    PlaybackState,
    PlaybackStatus,
    PlayerSummary,
    QueueEntry,
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
