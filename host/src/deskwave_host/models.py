"""Normalized media models shared by backends and the network API."""

from __future__ import annotations

from dataclasses import dataclass, field, replace
from enum import StrEnum
from time import time
from typing import Any


class PlaybackStatus(StrEnum):
    PLAYING = "playing"
    PAUSED = "paused"
    STOPPED = "stopped"


class RepeatMode(StrEnum):
    OFF = "off"
    TRACK = "track"
    PLAYLIST = "playlist"


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
    artwork_url: str | None = None
    artwork_id: str | None = None
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
    captured_at_ms: int = field(default_factory=lambda: int(time() * 1000))

    def normalized(self) -> PlaybackState:
        duration = None if self.duration_ms is None else max(0, self.duration_ms)
        position = max(0, self.position_ms)
        if duration is not None:
            position = min(position, duration)
        volume = self.volume
        if volume is not None:
            volume = min(1.0, max(0.0, volume))
        queue = tuple(entry.normalized() for entry in self.queue[:4])
        return replace(
            self,
            duration_ms=duration,
            position_ms=position,
            volume=volume,
            queue=queue,
            queue_available=bool(self.queue_available),
        )

    def content_key(self) -> tuple[Any, ...]:
        """Fields that should trigger an immediate state broadcast when changed."""

        return (
            self.title,
            self.artists,
            self.album,
            self.duration_ms,
            self.status,
            self.artwork_url,
            self.artwork_id,
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
        )

    def with_artwork(self, artwork_id: str | None) -> PlaybackState:
        return replace(self, artwork_id=artwork_id)

    def to_payload(self) -> dict[str, Any]:
        return {
            "title": self.title,
            "artists": list(self.artists),
            "album": self.album,
            "duration_ms": self.duration_ms,
            "position_ms": self.position_ms,
            "status": self.status.value,
            "artwork_id": self.artwork_id,
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
            "captured_at_ms": self.captured_at_ms,
        }


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
