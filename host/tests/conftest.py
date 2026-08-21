from __future__ import annotations

from dataclasses import replace
from pathlib import Path

import pytest

from deskwave_host.backends.base import MediaBackend, StateCallback
from deskwave_host.config import HostConfig, HostPaths
from deskwave_host.models import CommandResult, PlaybackState, PlaybackStatus, PlayerSummary


class FakeBackend(MediaBackend):
    def __init__(self, state: PlaybackState | None = None) -> None:
        self.state = state or PlaybackState()
        self.callback: StateCallback | None = None
        self.commands: list[tuple[str, dict[str, object]]] = []
        self.started = False

    async def start(self, callback: StateCallback) -> None:
        self.callback = callback
        self.started = True
        await callback(self.state)

    async def stop(self) -> None:
        self.started = False

    async def current_state(self) -> PlaybackState:
        return self.state

    async def command(self, command: str, arguments: dict[str, object]) -> CommandResult:
        self.commands.append((command, arguments))
        if command == "toggle":
            new_status = (
                PlaybackStatus.PAUSED
                if self.state.status is PlaybackStatus.PLAYING
                else PlaybackStatus.PLAYING
            )
            self.state = replace(self.state, status=new_status)
            if self.callback:
                await self.callback(self.state)
        return CommandResult(True)

    async def players(self) -> list[PlayerSummary]:
        if self.state.player_id is None:
            return []
        return [
            PlayerSummary(
                self.state.player_id,
                self.state.player_name or self.state.player_id,
                self.state.status,
            )
        ]

    async def emit(self, state: PlaybackState) -> None:
        self.state = state
        if self.callback:
            await self.callback(state)


@pytest.fixture
def host_config(tmp_path: Path) -> HostConfig:
    paths = HostPaths(
        config_dir=tmp_path / "config",
        cache_dir=tmp_path / "cache",
        state_dir=tmp_path / "state",
    )
    paths.ensure()
    return HostConfig(bind="127.0.0.1", port=8765, paths=paths)
