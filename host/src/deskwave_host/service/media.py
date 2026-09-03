"""Coordinates backend state, artwork, commands, and WebSocket subscribers."""

from __future__ import annotations

import asyncio
import hashlib
import logging

from deskwave_host.artwork import FALLBACK_THEME, ArtworkCache
from deskwave_host.backends.base import MediaBackend
from deskwave_host.lyrics import LyricsCache, LyricsDocument
from deskwave_host.models import (
    MAX_ARTWORK_GENERATION,
    CommandResult,
    LyricLine,
    LyricsStatus,
    PlaybackState,
    PlayerSummary,
    ThemePalette,
)

LOGGER = logging.getLogger("service")
ARTWORK_METADATA_GRACE_SECONDS = 1.0
ARTWORK_MAX_ATTEMPTS = 3
ARTWORK_RETRY_BACKOFF_SECONDS = (0.25, 0.75)


ArtworkContext = tuple[str, str]
TrackMetadata = tuple[str, tuple[str, ...], str]


class MediaService:
    def __init__(
        self,
        backend: MediaBackend,
        artwork: ArtworkCache,
        lyrics: LyricsCache | None = None,
    ) -> None:
        self._backend = backend
        self._artwork = artwork
        self._state = PlaybackState()
        self._subscribers: set[asyncio.Queue[PlaybackState]] = set()
        self._artwork_task: asyncio.Task[None] | None = None
        self._artwork_grace_task: asyncio.Task[None] | None = None
        self._artwork_context: ArtworkContext | None = None
        self._lyrics = lyrics
        self._lyrics_task: asyncio.Task[None] | None = None
        self._lyrics_track_key: str | None = None
        self._lyrics_document = LyricsDocument(LyricsStatus.UNAVAILABLE)
        self._lyrics_generation = 0
        self._track_key: str | None = None
        self._state_track_key: str | None = None
        self._identified_track_base: str | None = None
        self._identified_track_key: str | None = None
        self._identified_track_metadata: TrackMetadata | None = None
        self._intent_generation = 0
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
        pending = [
            task
            for task in (self._artwork_task, self._artwork_grace_task, self._lyrics_task)
            if task is not None
        ]
        for task in pending:
            task.cancel()
        if pending:
            await asyncio.gather(*pending, return_exceptions=True)
        self._artwork_task = None
        self._artwork_grace_task = None
        self._lyrics_task = None
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
        track_key = self._track_key_for(state)
        self._state_track_key = track_key
        new_intent = False
        if track_key is not None and track_key != self._track_key:
            self._begin_artwork_intent(track_key, state.artwork_url)
            new_intent = True
        elif track_key is not None and state.artwork_url:
            if self._artwork_context is None:
                # A delayed artUrl belongs to the existing generation unless its
                # no-art fallback has already been promoted.
                if (
                    self._intent_generation != 0
                    and self._state.artwork_generation == self._intent_generation
                ):
                    self._begin_artwork_intent(track_key, state.artwork_url)
                    new_intent = True
                else:
                    self._artwork_context = (state.artwork_url, track_key)
            elif self._artwork_context[0] != state.artwork_url:
                self._begin_artwork_intent(track_key, state.artwork_url)
                new_intent = True

        if track_key is not None and track_key != self._lyrics_track_key:
            self._begin_lyrics_intent(track_key, state)

        # Metadata is always current, but the validated visual tuple remains in
        # place until this intent resolves or authoritatively falls back.
        lyrics_status, lyric_lines = self._lyrics_snapshot(state.position_ms)
        self._state = state.with_artwork(
            self._state.artwork_id,
            self._state.theme,
            self._state.artwork_generation,
        ).with_lyrics(lyrics_status, lyric_lines)
        self._broadcast(self._state)

        if track_key is None:
            LOGGER.debug("Retaining promoted artwork through an empty backend snapshot")
            return
        if new_intent and not state.artwork_url:
            self._start_metadata_grace(track_key, self._intent_generation)
            return
        if not state.artwork_url:
            # Same-track pause and transient metadata gaps are not evidence that
            # a previously validated cover has disappeared.
            if (
                self._artwork_context is None
                and self._state.artwork_generation != self._intent_generation
                and (self._artwork_grace_task is None or self._artwork_grace_task.done())
            ):
                self._start_metadata_grace(track_key, self._intent_generation)
            return
        self._cancel_grace_task()
        artwork_context = (state.artwork_url, track_key)
        self._artwork_context = artwork_context
        if self._state.artwork_generation == self._intent_generation:
            return
        if self._artwork_task is not None and not self._artwork_task.done():
            return
        generation = self._intent_generation
        self._artwork_task = asyncio.create_task(
            self._resolve_artwork(state.artwork_url, artwork_context, generation),
            name=f"artwork-resolve-{generation}",
        )

    def _begin_lyrics_intent(self, track_key: str, state: PlaybackState) -> None:
        if self._lyrics_task is not None and not self._lyrics_task.done():
            self._lyrics_task.cancel()
        self._lyrics_task = None
        self._lyrics_track_key = track_key
        self._lyrics_generation = (
            1 if self._lyrics_generation >= MAX_ARTWORK_GENERATION else self._lyrics_generation + 1
        )
        if (
            self._lyrics is None
            or not state.title.strip()
            or not any(artist.strip() for artist in state.artists)
        ):
            self._lyrics_document = LyricsDocument(LyricsStatus.UNAVAILABLE)
            return
        self._lyrics_document = LyricsDocument(LyricsStatus.LOADING)
        generation = self._lyrics_generation
        self._lyrics_task = asyncio.create_task(
            self._resolve_lyrics(track_key, generation, state),
            name=f"lyrics-resolve-{generation}",
        )

    def _lyrics_snapshot(self, position_ms: int) -> tuple[LyricsStatus, tuple[LyricLine, ...]]:
        return self._lyrics_document.status, self._lyrics_document.window(position_ms)

    async def _resolve_lyrics(
        self,
        track_key: str,
        generation: int,
        state: PlaybackState,
    ) -> None:
        try:
            if self._lyrics is None:
                return
            result = await self._lyrics.resolve(state)
            if (
                generation != self._lyrics_generation
                or track_key != self._lyrics_track_key
                or track_key != self._state_track_key
            ):
                LOGGER.debug("Rejected stale lyrics generation %d", generation)
                return
            self._lyrics_document = result
            status, lines = self._lyrics_snapshot(self._state.position_ms)
            self._state = self._state.with_lyrics(status, lines)
            self._broadcast(self._state)
        except asyncio.CancelledError:
            raise
        finally:
            if self._lyrics_task is asyncio.current_task():
                self._lyrics_task = None

    def _begin_artwork_intent(self, track_key: str, artwork_url: str | None) -> None:
        self._cancel_artwork_task()
        self._cancel_grace_task()
        self._intent_generation = (
            1 if self._intent_generation >= MAX_ARTWORK_GENERATION else self._intent_generation + 1
        )
        self._track_key = track_key
        self._artwork_context = None if not artwork_url else (artwork_url, track_key)
        LOGGER.debug(
            "Started artwork intent generation %d for track %.12s",
            self._intent_generation,
            hashlib.sha256(track_key.encode()).hexdigest(),
        )

    def _start_metadata_grace(self, track_key: str, generation: int) -> None:
        self._cancel_grace_task()
        self._artwork_grace_task = asyncio.create_task(
            self._await_artwork_metadata(track_key, generation),
            name=f"artwork-grace-{generation}",
        )

    async def _await_artwork_metadata(self, track_key: str, generation: int) -> None:
        try:
            await asyncio.sleep(ARTWORK_METADATA_GRACE_SECONDS)
        except asyncio.CancelledError:
            raise
        else:
            if (
                self._intent_generation != generation
                or self._track_key != track_key
                or self._artwork_context is not None
                or self._state_track_key != track_key
            ):
                return
            LOGGER.debug(
                "Artwork generation %d reached the no-art metadata grace deadline",
                generation,
            )
            self._promote_visual(None, FALLBACK_THEME, generation, "metadata grace expired")
        finally:
            if self._artwork_grace_task is asyncio.current_task():
                self._artwork_grace_task = None

    async def _resolve_artwork(
        self,
        artwork_url: str,
        artwork_context: ArtworkContext,
        generation: int,
    ) -> None:
        try:
            for attempt in range(1, ARTWORK_MAX_ATTEMPTS + 1):
                if not self._intent_matches(artwork_context, generation):
                    self._log_stale_artwork(artwork_context, generation, "pre-attempt")
                    return
                resolved = await self._artwork.resolve(
                    artwork_url,
                    cache_variant=artwork_context[1],
                )
                if not self._intent_matches(artwork_context, generation):
                    self._log_stale_artwork(artwork_context, generation, "post-resolve")
                    return
                if resolved is not None:
                    self._promote_visual(
                        resolved.artwork_id,
                        resolved.theme,
                        generation,
                        "artwork resolved",
                    )
                    return
                if attempt < ARTWORK_MAX_ATTEMPTS:
                    delay = ARTWORK_RETRY_BACKOFF_SECONDS[attempt - 1]
                    LOGGER.debug(
                        "Artwork generation %d attempt %d/%d failed; retrying in %.2fs",
                        generation,
                        attempt,
                        ARTWORK_MAX_ATTEMPTS,
                        delay,
                    )
                    await asyncio.sleep(delay)
            if self._intent_matches(artwork_context, generation):
                if self._state_track_key is None or not self._state.artwork_url:
                    # An empty or artUrl-less snapshot cannot authoritatively
                    # reject the last cover. Rearm this intent when metadata returns.
                    self._artwork_context = None
                    LOGGER.debug(
                        "Deferred artwork generation %d fallback during a metadata gap",
                        generation,
                    )
                elif self._state_track_key == artwork_context[1]:
                    self._promote_visual(
                        None,
                        FALLBACK_THEME,
                        generation,
                        "artwork retries exhausted",
                    )
        except asyncio.CancelledError:
            raise
        finally:
            if self._artwork_task is asyncio.current_task():
                self._artwork_task = None

    def _intent_matches(self, artwork_context: ArtworkContext, generation: int) -> bool:
        return (
            self._intent_generation == generation
            and self._track_key == artwork_context[1]
            and self._artwork_context == artwork_context
        )

    @staticmethod
    def _log_stale_artwork(
        artwork_context: ArtworkContext,
        generation: int,
        phase: str,
    ) -> None:
        context_hash = hashlib.sha256(
            "\x00".join(artwork_context).encode("utf-8", errors="replace")
        ).hexdigest()
        LOGGER.debug(
            "Rejected stale artwork generation %d at %s (context=%.12s)",
            generation,
            phase,
            context_hash,
        )

    def _promote_visual(
        self,
        artwork_id: str | None,
        theme: ThemePalette,
        generation: int,
        reason: str,
    ) -> None:
        if generation != self._intent_generation:
            return
        self._state = self._state.with_artwork(artwork_id, theme, generation)
        LOGGER.debug(
            "Promoted artwork generation %d (%s, id=%s)",
            generation,
            reason,
            "fallback" if artwork_id is None else artwork_id[:12],
        )
        self._broadcast(self._state)

    def _cancel_artwork_task(self) -> None:
        if self._artwork_task is not None and not self._artwork_task.done():
            self._artwork_task.cancel()
        self._artwork_task = None

    def _cancel_grace_task(self) -> None:
        if self._artwork_grace_task is not None and not self._artwork_grace_task.done():
            self._artwork_grace_task.cancel()
        self._artwork_grace_task = None

    def _track_key_for(self, state: PlaybackState) -> str | None:
        """Identify a track independently from delayed or temporarily absent artUrl.

        Track IDs are strongest, but some players reuse a constant ID. Nonempty
        metadata contradictions disambiguate those tracks, while absent fields
        inherit the last signature so a transient metadata gap stays in place.
        """

        player_key = state.player_id or state.player_name or "unknown-player"
        if state.track_id:
            base = f"id\x1f{player_key}\x1f{state.track_id}"
            metadata: TrackMetadata = (
                state.title,
                tuple(artist for artist in state.artists if artist),
                state.album,
            )
            if base != self._identified_track_base:
                self._identified_track_base = base
                self._identified_track_metadata = metadata
                self._identified_track_key = self._identified_key(base, metadata)
                return self._identified_track_key

            previous = self._identified_track_metadata or ("", (), "")
            if not any((metadata[0], metadata[1], metadata[2])):
                return self._identified_track_key or base
            contradiction = any(
                old and new and old != new for old, new in zip(previous, metadata, strict=True)
            )
            visual_promoted = (
                self._intent_generation != 0
                and self._state.artwork_generation == self._intent_generation
            )
            # Additions while artwork is pending are ordinary delayed metadata.
            # After commit, the same addition disambiguates a reused ID/URL.
            addition_after_promotion = visual_promoted and any(
                not old and bool(new) for old, new in zip(previous, metadata, strict=True)
            )
            if contradiction or addition_after_promotion:
                self._identified_track_metadata = metadata
                self._identified_track_key = self._identified_key(base, metadata)
                return self._identified_track_key
            self._identified_track_metadata = (
                metadata[0] or previous[0],
                metadata[1] or previous[1],
                metadata[2] or previous[2],
            )
            return self._identified_track_key or base
        if state.title or state.artists or state.album:
            return "\x1f".join(("metadata", player_key, state.title, *state.artists, state.album))
        if state.artwork_url:
            return f"url\x1f{player_key}\x1f{state.artwork_url}"
        return None

    @staticmethod
    def _identified_key(base: str, metadata: TrackMetadata) -> str:
        if not any((metadata[0], metadata[1], metadata[2])):
            return base
        return "\x1f".join((base, "metadata", metadata[0], *metadata[1], metadata[2]))

    def _broadcast(self, state: PlaybackState) -> None:
        for queue in tuple(self._subscribers):
            if queue.full():
                try:
                    queue.get_nowait()
                except asyncio.QueueEmpty:
                    pass
            queue.put_nowait(state)
