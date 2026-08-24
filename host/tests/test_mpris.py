from time import monotonic

import pytest
from dbus_next.errors import InterfaceNotFoundError

from deskwave_host.backends import mpris as mpris_module
from deskwave_host.backends.mpris import MAX_SNAPSHOT_FAILURES, MPRISBackend
from deskwave_host.models import PlaybackState


class FailingPlayer:
    async def snapshot(self) -> PlaybackState:
        raise RuntimeError("player stopped responding")


class PlayerNameBus:
    async def call_list_names(self) -> list[str]:
        return ["org.mpris.MediaPlayer2.incomplete"]


class QueueProperties:
    async def call_get_all(self, interface: str) -> dict[str, object]:
        assert interface == mpris_module.TRACKLIST_INTERFACE
        return {"Tracks": ["/track/current", "/track/next", "/track/following"]}


class QueueTrackList:
    async def call_get_tracks_metadata(self, track_ids: list[str]) -> list[dict[str, object]]:
        assert track_ids == ["/track/current", "/track/next", "/track/following"]
        return [
            {"mpris:trackid": "/track/current", "xesam:title": "Current"},
            {
                "mpris:trackid": "/track/next",
                "xesam:title": "Next",
                "xesam:artist": ["Artist two"],
            },
            {
                "mpris:trackid": "/track/following",
                "xesam:title": "Following",
                "xesam:artist": ["Artist three"],
            },
        ]


async def test_tracklist_returns_entries_after_current_track() -> None:
    player = mpris_module._MPRISPlayer(
        player_id="org.mpris.MediaPlayer2.test",
        player=object(),
        properties=QueueProperties(),
        tracklist=QueueTrackList(),
    )

    queue, available = await player.queue_snapshot("/track/current")

    assert available is True
    assert [(entry.title, entry.artist) for entry in queue] == [
        ("Next", "Artist two"),
        ("Following", "Artist three"),
    ]


async def test_player_missing_mpris_interface_is_skipped(monkeypatch: pytest.MonkeyPatch) -> None:
    backend = MPRISBackend()
    backend._bus = object()  # type: ignore[assignment]
    backend._dbus = PlayerNameBus()

    async def incomplete_player(*_arguments: object) -> mpris_module._MPRISPlayer:
        raise InterfaceNotFoundError("org.mpris.MediaPlayer2.Player")

    monkeypatch.setattr(mpris_module._MPRISPlayer, "connect", incomplete_player)

    await backend._poll_once()

    assert backend._players == {}


async def test_persistently_unresponsive_player_state_expires() -> None:
    player_id = "org.mpris.MediaPlayer2.hung"
    backend = MPRISBackend()
    backend._players[player_id] = FailingPlayer()  # type: ignore[assignment]
    backend._snapshots[player_id] = PlaybackState(player_id=player_id, title="Stale")
    backend._current = backend._snapshots[player_id]
    backend._last_scan = monotonic()

    for _ in range(MAX_SNAPSHOT_FAILURES):
        await backend._poll_once()

    assert player_id not in backend._snapshots
    assert backend._current.player_id is None


async def test_short_mpris_reconnect_retains_last_snapshot(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    backend = MPRISBackend()
    retained = PlaybackState(player_id="org.mpris.MediaPlayer2.test", track_id="track-1")
    backend._current = retained
    published: list[PlaybackState] = []

    async def capture(state: PlaybackState) -> None:
        published.append(state)

    backend._callback = capture
    now = 100.0
    monkeypatch.setattr(mpris_module, "monotonic", lambda: now)

    await backend._hold_state_during_reconnect()

    assert backend._current == retained
    assert published == []

    now += mpris_module.MPRIS_RECONNECT_STATE_GRACE_SECONDS + 0.1
    await backend._hold_state_during_reconnect()

    assert backend._current.player_id is None
    assert len(published) == 1
    assert published[0].player_id is None
