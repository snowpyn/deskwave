"""Normalized media models shared by backends and the network API."""

from __future__ import annotations

from dataclasses import dataclass, field, replace
from enum import StrEnum
from time import time
from typing import Any

MAX_ARTWORK_GENERATION = 0xFFFF_FFFF
MAX_LYRIC_WINDOW_LINES = 5


@dataclass(frozen=True, slots=True)
class ThemePalette:
    """Four packed 0xRRGGBB colors safe to publish to a device."""

    primary: int
    secondary: int
    background: int
    foreground: int

    def __post_init__(self) -> None:
        for name, value in (
            ("primary", self.primary),
            ("secondary", self.secondary),
            ("background", self.background),
            ("foreground", self.foreground),
        ):
            if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFF_FFFF:
                raise ValueError(f"theme {name} must be a packed 24-bit RGB integer")

    def to_payload(self) -> dict[str, int]:
        return {
            "primary": self.primary,
            "secondary": self.secondary,
            "background": self.background,
            "foreground": self.foreground,
        }


class PlaybackStatus(StrEnum):
    PLAYING = "playing"
    PAUSED = "paused"
    STOPPED = "stopped"


class MediaKind(StrEnum):
    """The presentation family for the active media item."""

    MUSIC = "music"
    PODCAST = "podcast"


class RepeatMode(StrEnum):
    OFF = "off"
    TRACK = "track"
    PLAYLIST = "playlist"


class LyricsStatus(StrEnum):
    LOADING = "loading"
    SYNCED = "synced"
    INSTRUMENTAL = "instrumental"
    UNAVAILABLE = "unavailable"


@dataclass(frozen=True, slots=True)
class LyricLine:
    """One timestamped text line selected for the device's rolling window."""

    time_ms: int
    text: str

    def normalized(self) -> LyricLine:
        return replace(
            self,
            time_ms=max(0, min(self.time_ms, 7 * 24 * 60 * 60 * 1000)),
            text=self.text.replace("\x00", "").strip()[:512],
        )


@dataclass(frozen=True, slots=True)
class QueueEntry:
    """One upcoming track exposed by a media backend."""

    title: str = ""
    artist: str = ""
    track_id: str | None = None

    def normalized(self) -> QueueEntry:
        return replace(
            self,
            title=self.title.replace("\x00", "").strip()[:256],
            artist=self.artist.replace("\x00", "").strip()[:160],
            track_id=None if self.track_id is None else self.track_id[:512],
        )


@dataclass(frozen=True, slots=True)
class PlaybackState:
    """Backend-neutral snapshot. Durations and positions use milliseconds."""

    title: str = ""
    artists: tuple[str, ...] = ()
    album: str = ""
    duration_ms: int | None = None
    position_ms: int = 0
    status: PlaybackStatus = PlaybackStatus.STOPPED
    media_kind: MediaKind = MediaKind.MUSIC
    artwork_url: str | None = None
    artwork_id: str | None = None
    theme: ThemePalette | None = None
    artwork_generation: int = 0
    volume: float | None = None
    muted: bool | None = None
    shuffle: bool | None = None
    repeat: RepeatMode | None = None
    player_id: str | None = None
    player_name: str | None = None
    track_id: str | None = None
    can_seek: bool = False
    can_next: bool = False
    can_previous: bool = False
    can_control: bool = False
    queue: tuple[QueueEntry, ...] = ()
    queue_available: bool = False
    lyrics_status: LyricsStatus = LyricsStatus.UNAVAILABLE
    lyrics: tuple[LyricLine, ...] = ()
    captured_at_ms: int = field(default_factory=lambda: int(time() * 1000))

    def __post_init__(self) -> None:
        if (
            isinstance(self.artwork_generation, bool)
            or not isinstance(self.artwork_generation, int)
            or not 0 <= self.artwork_generation <= MAX_ARTWORK_GENERATION
        ):
            raise ValueError("artwork_generation must be an unsigned 32-bit integer")

    def normalized(self) -> PlaybackState:
        duration = None if self.duration_ms is None else max(0, self.duration_ms)
        position = max(0, self.position_ms)
        if duration is not None:
            position = min(position, duration)
        volume = self.volume
        if volume is not None:
            volume = min(1.0, max(0.0, volume))
        queue = tuple(entry.normalized() for entry in self.queue[:4])
        lyrics = tuple(
            line.normalized() for line in self.lyrics[:MAX_LYRIC_WINDOW_LINES] if line.text.strip()
        )
        return replace(
            self,
            duration_ms=duration,
            position_ms=position,
            volume=volume,
            queue=queue,
            queue_available=bool(self.queue_available),
            lyrics=lyrics,
        )

    def content_key(self) -> tuple[Any, ...]:
        """Fields that should trigger an immediate state broadcast when changed."""

        return (
            self.title,
            self.artists,
            self.album,
            self.duration_ms,
            self.status,
            self.media_kind,
            self.artwork_url,
            self.artwork_id,
            self.theme,
            self.artwork_generation,
            self.volume,
            self.muted,
            self.shuffle,
            self.repeat,
            self.player_id,
            self.player_name,
            self.track_id,
            self.can_seek,
            self.can_next,
            self.can_previous,
            self.can_control,
            self.queue,
            self.queue_available,
            self.lyrics_status,
            self.lyrics,
        )

    def with_artwork(
        self,
        artwork_id: str | None,
        theme: ThemePalette | None,
        artwork_generation: int,
    ) -> PlaybackState:
        """Return a state with one atomically associated visual-theme tuple."""

        return replace(
            self,
            artwork_id=artwork_id,
            theme=theme,
            artwork_generation=artwork_generation,
        )

    def with_lyrics(
        self,
        status: LyricsStatus,
        lines: tuple[LyricLine, ...],
    ) -> PlaybackState:
        """Return a state with the bounded lyric window for its current position."""

        return replace(self, lyrics_status=status, lyrics=lines).normalized()

    def to_payload(self) -> dict[str, Any]:
        captions = {
            "status": self.lyrics_status.value,
            "lines": [{"time_ms": line.time_ms, "text": line.text} for line in self.lyrics],
        }
        payload = {
            "title": self.title,
            "artists": list(self.artists),
            "album": self.album,
            "duration_ms": self.duration_ms,
            "position_ms": self.position_ms,
            "status": self.status.value,
            "media_kind": self.media_kind.value,
            "artwork_id": self.artwork_id,
            "theme": None if self.theme is None else self.theme.to_payload(),
            "artwork_generation": self.artwork_generation,
            "volume": self.volume,
            "muted": self.muted,
            "shuffle": self.shuffle,
            "repeat": None if self.repeat is None else self.repeat.value,
            "player_id": self.player_id,
            "player_name": self.player_name,
            "track_id": self.track_id,
            "capabilities": {
                "seek": self.can_seek,
                "next": self.can_next,
                "previous": self.can_previous,
                "control": self.can_control,
                "queue": self.queue_available,
            },
            "queue": [
                {"title": entry.title, "artist": entry.artist, "track_id": entry.track_id}
                for entry in self.queue
            ],
            "lyrics": captions,
            "captured_at_ms": self.captured_at_ms,
        }
        if self.media_kind is MediaKind.PODCAST:
            payload["transcript"] = payload.pop("lyrics")
        return payload


@dataclass(frozen=True, slots=True)
class CommandResult:
    success: bool
    error: str | None = None


@dataclass(frozen=True, slots=True)
class PlayerSummary:
    player_id: str
    name: str
    status: PlaybackStatus


def select_active_player(
    players: list[PlayerSummary],
    previous_id: str | None = None,
    preferred_id: str | None = None,
) -> PlayerSummary | None:
    """Select predictably: playing, explicit preference, previous, then sorted."""

    if not players:
        return None
    playing = sorted(
        (player for player in players if player.status is PlaybackStatus.PLAYING),
        key=lambda player: player.player_id,
    )
    if playing:
        if preferred_id:
            preferred_playing = next(
                (player for player in playing if player.player_id == preferred_id), None
            )
            if preferred_playing:
                return preferred_playing
        if previous_id:
            previous_playing = next(
                (player for player in playing if player.player_id == previous_id), None
            )
            if previous_playing:
                return previous_playing
        return playing[0]
    if preferred_id:
        preferred = next((player for player in players if player.player_id == preferred_id), None)
        if preferred:
            return preferred
    if previous_id:
        previous = next((player for player in players if player.player_id == previous_id), None)
        if previous:
            return previous
    return min(players, key=lambda player: player.player_id)
