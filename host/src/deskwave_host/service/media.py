"""Coordinates backend state, artwork, commands, and WebSocket subscribers."""

from __future__ import annotations

import asyncio
import logging
from time import monotonic

from deskwave_host.artwork import ArtworkCache
from deskwave_host.backends.base import MediaBackend
from deskwave_host.models import CommandResult, PlaybackState, PlayerSummary

LOGGER = logging.getLogger("service")
ARTWORK_RETRY_SECONDS = 5.0


ArtworkContext = tuple[str, str]


class MediaService:
    def __init__(self, backend: MediaBackend, artwork: ArtworkCache) -> None:
        self._backend = backend
        self._artwork = artwork
        self._state = PlaybackState()
        self._subscribers: set[asyncio.Queue[PlaybackState]] = set()
        self._artwork_task: asyncio.Task[None] | None = None
        self._artwork_context: ArtworkContext | None = None
        self._last_artwork_attempt = 0.0
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
        artwork_context = self._context_for(state)
        same_artwork = artwork_context == self._artwork_context
        retained_artwork = self._state.artwork_id if same_artwork else None
        self._state = state.with_artwork(retained_artwork)
        self._broadcast(self._state)
        if not state.artwork_url:
            if self._artwork_task is not None:
                self._artwork_task.cancel()
            self._artwork_task = None
            self._artwork_context = None
            self._last_artwork_attempt = 0.0
            return
        if artwork_context is None:
            return
        task_running = self._artwork_task is not None and not self._artwork_task.done()
        now = monotonic()
        if same_artwork and (
            retained_artwork is not None
            or task_running
            or now - self._last_artwork_attempt < ARTWORK_RETRY_SECONDS
        ):
            return
        if self._artwork_task is not None:
            self._artwork_task.cancel()
        self._artwork_context = artwork_context
        self._last_artwork_attempt = now
        self._artwork_task = asyncio.create_task(
            self._resolve_artwork(state.artwork_url, artwork_context), name="artwork-resolve"
        )

    async def _resolve_artwork(self, artwork_url: str, artwork_context: ArtworkContext) -> None:
        try:
            artwork_id = await self._artwork.resolve(artwork_url)
        except asyncio.CancelledError:
            raise
        else:
            if self._context_for(self._state) != artwork_context:
                return
            self._state = self._state.with_artwork(artwork_id)
            self._broadcast(self._state)
        finally:
            if self._artwork_task is asyncio.current_task():
                self._artwork_task = None

    @staticmethod
    def _context_for(state: PlaybackState) -> ArtworkContext | None:
        """Identify the artwork request's track, not just its source URL.

        Several MPRIS players reuse a single local file URL while replacing its
        contents for each track.  A URL-only key would retain the previous
        album cover forever after a skip.  Track IDs are the strongest identity;
        the normalized metadata fallback covers players that omit them.
        """

        if not state.artwork_url:
            return None
        track_key = state.track_id
        if not track_key:
            track_key = "\x1f".join((state.title, *state.artists, state.album))
        return state.artwork_url, track_key

    def _broadcast(self, state: PlaybackState) -> None:
        for queue in tuple(self._subscribers):
            if queue.full():
                try:
                    queue.get_nowait()
                except asyncio.QueueEmpty:
                    pass
            queue.put_nowait(state)
