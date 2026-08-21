"""Backend contract for host media integrations."""

from __future__ import annotations

from abc import ABC, abstractmethod
from collections.abc import Awaitable, Callable

from deskwave_host.models import CommandResult, PlaybackState, PlayerSummary

StateCallback = Callable[[PlaybackState], Awaitable[None]]


class MediaBackend(ABC):
    @abstractmethod
    async def start(self, callback: StateCallback) -> None:
        """Start producing state updates without blocking the caller."""

    @abstractmethod
    async def stop(self) -> None:
        """Stop background work and release external resources."""

    @abstractmethod
    async def current_state(self) -> PlaybackState:
        """Return the most recently confirmed state."""

    @abstractmethod
    async def command(self, command: str, arguments: dict[str, object]) -> CommandResult:
        """Execute a validated playback command."""

    @abstractmethod
    async def players(self) -> list[PlayerSummary]:
        """Return currently detected media players."""
