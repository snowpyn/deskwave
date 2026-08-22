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
