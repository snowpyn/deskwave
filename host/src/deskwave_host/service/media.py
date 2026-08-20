"""Coordinates backend state, artwork, commands, and WebSocket subscribers."""

from __future__ import annotations

import asyncio
import logging

from deskwave_host.artwork import ArtworkCache
from deskwave_host.backends.base import MediaBackend
from deskwave_host.models import CommandResult, PlaybackState, PlayerSummary

LOGGER = logging.getLogger("service")


class MediaService:
    def __init__(self, backend: MediaBackend, artwork: ArtworkCache) -> None:
        self._backend = backend
        self._artwork = artwork
        self._state = PlaybackState()
        self._subscribers: set[asyncio.Queue[PlaybackState]] = set()
        self._artwork_task: asyncio.Task[None] | None = None
        self._started = False

    async def start(self) -> None:
        if self._started:
            return
        self._started = True
        await self._backend.start(self._on_backend_state)

    async def stop(self) -> None:
        if not self._started:
            return
        self._started = False
        if self._artwork_task is not None:
            self._artwork_task.cancel()
            try:
                await self._artwork_task
            except asyncio.CancelledError:
                pass
            self._artwork_task = None
        await self._backend.stop()

    @property
    def state(self) -> PlaybackState:
        return self._state

    async def players(self) -> list[PlayerSummary]:
        return await self._backend.players()

    def subscribe(self) -> asyncio.Queue[PlaybackState]:
        queue: asyncio.Queue[PlaybackState] = asyncio.Queue(maxsize=1)
        self._subscribers.add(queue)
        return queue

    def unsubscribe(self, queue: asyncio.Queue[PlaybackState]) -> None:
        self._subscribers.discard(queue)

    async def command(self, command: str, arguments: dict[str, object]) -> CommandResult:
        try:
            async with asyncio.timeout(2.5):
                return await self._backend.command(command, arguments)
        except TimeoutError:
            LOGGER.warning("Command %s exceeded the service timeout", command)
            return CommandResult(False, "host command timeout")

    async def _on_backend_state(self, state: PlaybackState) -> None:
        self._state = state.with_artwork(None)
        self._broadcast(self._state)
        if self._artwork_task is not None:
            self._artwork_task.cancel()
        if state.artwork_url:
            self._artwork_task = asyncio.create_task(
                self._resolve_artwork(state), name="artwork-resolve"
            )
        else:
            self._artwork_task = None

    async def _resolve_artwork(self, source_state: PlaybackState) -> None:
        try:
            artwork_id = await self._artwork.resolve(source_state.artwork_url or "")
        except asyncio.CancelledError:
            raise
        if (
            self._state.track_id != source_state.track_id
            or self._state.artwork_url != source_state.artwork_url
        ):
            return
        self._state = self._state.with_artwork(artwork_id)
        self._broadcast(self._state)

    def _broadcast(self, state: PlaybackState) -> None:
        for queue in tuple(self._subscribers):
            if queue.full():
                try:
                    queue.get_nowait()
                except asyncio.QueueEmpty:
                    pass
            queue.put_nowait(state)
