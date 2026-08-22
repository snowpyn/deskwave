"""Linux MPRIS backend implemented over the session D-Bus."""

from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass
from time import monotonic, time
from typing import Any

from dbus_next import DBusError, Variant  # type: ignore[attr-defined]
from dbus_next.aio import MessageBus  # type: ignore[attr-defined]
from dbus_next.constants import BusType
from dbus_next.errors import InterfaceNotFoundError

from deskwave_host.backends.base import MediaBackend, StateCallback
from deskwave_host.models import (
    CommandResult,
    PlaybackState,
    PlaybackStatus,
    PlayerSummary,
    QueueEntry,
    RepeatMode,
    select_active_player,
)

LOGGER = logging.getLogger("backend.mpris")
MPRIS_PREFIX = "org.mpris.MediaPlayer2."
MPRIS_PATH = "/org/mpris/MediaPlayer2"
PLAYER_INTERFACE = "org.mpris.MediaPlayer2.Player"
ROOT_INTERFACE = "org.mpris.MediaPlayer2"
TRACKLIST_INTERFACE = "org.mpris.MediaPlayer2.TrackList"
PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties"
POLL_SECONDS = 0.4
PLAYER_SCAN_SECONDS = 2.0
POSITION_SYNC_SECONDS = 2.0
MAX_SNAPSHOT_FAILURES = 3
MAX_TRACKLIST_ITEMS = 64
MAX_QUEUE_ENTRIES = 4


def _value(properties: dict[str, Any], key: str, default: Any = None) -> Any:
    value = properties.get(key, default)
    return value.value if isinstance(value, Variant) else value


def _safe_text(value: object, maximum: int = 512) -> str:
    if value is None:
        return ""
    text = str(value).replace("\x00", "").strip()
    return text[:maximum]


def _status(value: object) -> PlaybackStatus:
    return {
        "Playing": PlaybackStatus.PLAYING,
        "Paused": PlaybackStatus.PAUSED,
        "Stopped": PlaybackStatus.STOPPED,
    }.get(str(value), PlaybackStatus.STOPPED)


def _repeat(value: object) -> RepeatMode | None:
    return {
        "None": RepeatMode.OFF,
        "Track": RepeatMode.TRACK,
        "Playlist": RepeatMode.PLAYLIST,
    }.get(str(value))


@dataclass(slots=True)
class _MPRISPlayer:
    player_id: str
    player: Any
    properties: Any
    tracklist: Any | None

    @classmethod
    async def connect(cls, bus: MessageBus, player_id: str) -> _MPRISPlayer:
        introspection = await asyncio.wait_for(bus.introspect(player_id, MPRIS_PATH), timeout=1.5)
        proxy = bus.get_proxy_object(player_id, MPRIS_PATH, introspection)
        try:
            tracklist = proxy.get_interface(TRACKLIST_INTERFACE)
        except InterfaceNotFoundError:
            tracklist = None
        return cls(
            player_id=player_id,
            player=proxy.get_interface(PLAYER_INTERFACE),
            properties=proxy.get_interface(PROPERTIES_INTERFACE),
            tracklist=tracklist,
        )

    async def queue_snapshot(
        self, current_track_id: str | None
    ) -> tuple[tuple[QueueEntry, ...], bool]:
        """Return bounded upcoming TrackList entries when the player exposes TrackList."""

        if self.tracklist is None:
            return (), False
        try:
            track_properties = await asyncio.wait_for(
                self.properties.call_get_all(TRACKLIST_INTERFACE), timeout=1.0
            )
            if not isinstance(track_properties, dict):
                return (), False
            track_ids = _value(track_properties, "TrackList", [])
            if not isinstance(track_ids, (list, tuple)):
                return (), True
            bounded_ids = [
                track_id
                for track_id in track_ids[:MAX_TRACKLIST_ITEMS]
                if isinstance(track_id, str)
            ]
            if not bounded_ids:
                return (), True
            metadata = await asyncio.wait_for(
                self.tracklist.call_get_tracks_metadata(bounded_ids), timeout=1.0
            )
        except (DBusError, OSError, RuntimeError, TimeoutError, TypeError, ValueError) as error:
            LOGGER.debug("Could not refresh MPRIS queue for %s: %s", self.player_id, error)
            return (), False

        if not isinstance(metadata, (list, tuple)):
            return (), False
        entries: list[QueueEntry] = []
        current_index: int | None = None
        for track_id, values in zip(bounded_ids, metadata, strict=False):
            if not isinstance(values, dict):
                continue
            entry_track_id = _safe_text(_value(values, "mpris:trackid", track_id), 512) or track_id
            if current_track_id is not None and entry_track_id == current_track_id:
                current_index = len(entries)
            artists_value = _value(values, "xesam:artist", [])
            if isinstance(artists_value, (list, tuple)):
                artist = ", ".join(_safe_text(artist, 160) for artist in artists_value[:3])
            else:
                artist = _safe_text(artists_value, 160)
            entries.append(
                QueueEntry(
                    title=_safe_text(_value(values, "xesam:title"), 256),
                    artist=artist,
                    track_id=entry_track_id,
                ).normalized()
            )
        start = 0 if current_index is None else current_index + 1
        return tuple(entries[start : start + MAX_QUEUE_ENTRIES]), True

    async def snapshot(self) -> PlaybackState:
        player_properties, root_properties = await asyncio.wait_for(
            asyncio.gather(
                self.properties.call_get_all(PLAYER_INTERFACE),
                self.properties.call_get_all(ROOT_INTERFACE),
            ),
            timeout=1.5,
        )
        metadata = _value(player_properties, "Metadata", {})
        if not isinstance(metadata, dict):
            metadata = {}
        artists_value = _value(metadata, "xesam:artist", [])
        artists = (
            tuple(_safe_text(artist, 160) for artist in artists_value[:8])
            if isinstance(artists_value, list)
            else ()
        )
        duration_us = _value(metadata, "mpris:length")
        duration_ms = int(duration_us // 1000) if isinstance(duration_us, int) else None
        position_us = _value(player_properties, "Position", 0)
        position_ms = int(position_us // 1000) if isinstance(position_us, int) else 0
        volume_value = _value(player_properties, "Volume")
        volume = float(volume_value) if isinstance(volume_value, (int, float)) else None
        artwork = _safe_text(_value(metadata, "mpris:artUrl"), 2048) or None
        track_id = _safe_text(_value(metadata, "mpris:trackid"), 512) or None
        queue, queue_available = await self.queue_snapshot(track_id)
        return PlaybackState(
            title=_safe_text(_value(metadata, "xesam:title"), 256),
            artists=artists,
            album=_safe_text(_value(metadata, "xesam:album"), 256),
            duration_ms=duration_ms,
            position_ms=position_ms,
            status=_status(_value(player_properties, "PlaybackStatus")),
            artwork_url=artwork,
            volume=volume,
            muted=None if volume is None else volume <= 0.0001,
            shuffle=(
                bool(_value(player_properties, "Shuffle"))
                if "Shuffle" in player_properties
                else None
            ),
            repeat=_repeat(_value(player_properties, "LoopStatus")),
            player_id=self.player_id,
            player_name=_safe_text(_value(root_properties, "Identity"), 128)
            or self.player_id.removeprefix(MPRIS_PREFIX),
            track_id=track_id,
            can_seek=bool(_value(player_properties, "CanSeek", False)),
            can_next=bool(_value(player_properties, "CanGoNext", False)),
            can_previous=bool(_value(player_properties, "CanGoPrevious", False)),
            can_control=bool(_value(player_properties, "CanControl", False)),
            queue=queue,
            queue_available=queue_available,
            captured_at_ms=int(time() * 1000),
        ).normalized()


class MPRISBackend(MediaBackend):
    """Auto-reconnecting MPRIS player manager with deterministic selection."""

    def __init__(self, preferred_player: str | None = None) -> None:
        self._preferred_player = preferred_player
        self._bus: MessageBus | None = None
        self._dbus: Any = None
        self._players: dict[str, _MPRISPlayer] = {}
        self._snapshots: dict[str, PlaybackState] = {}
        self._snapshot_failures: dict[str, int] = {}
        self._current = PlaybackState()
        self._callback: StateCallback | None = None
        self._poll_task: asyncio.Task[None] | None = None
        self._stop_event = asyncio.Event()
        self._wake_event = asyncio.Event()
        self._last_scan = 0.0
        self._last_publish = 0.0
        self._muted_restore_volume = 0.5

    async def start(self, callback: StateCallback) -> None:
        if self._poll_task is not None:
            return
        self._callback = callback
        self._stop_event.clear()
        self._poll_task = asyncio.create_task(self._poll(), name="mpris-poll")

    async def stop(self) -> None:
        self._stop_event.set()
        self._wake_event.set()
        if self._poll_task is not None:
            await self._poll_task
            self._poll_task = None
        if self._bus is not None:
            self._bus.disconnect()  # type: ignore[no-untyped-call]
        self._bus = None
        self._dbus = None
        self._players.clear()
        self._snapshots.clear()
        self._snapshot_failures.clear()

    async def current_state(self) -> PlaybackState:
        return self._current

    async def players(self) -> list[PlayerSummary]:
        return [
            PlayerSummary(player_id, snapshot.player_name or player_id, snapshot.status)
            for player_id, snapshot in sorted(self._snapshots.items())
        ]

    async def command(self, command: str, arguments: dict[str, object]) -> CommandResult:
        player_id = self._current.player_id
        client = self._players.get(player_id or "")
        if command == "select_player":
            requested = arguments.get("player_id")
            if not isinstance(requested, str) or requested not in self._players:
                return CommandResult(False, "requested player is not available")
            self._preferred_player = requested
            self._current = self._snapshots[requested]
            await self._publish(force=True)
            return CommandResult(True)
        if command == "refresh":
            self._wake_event.set()
            return CommandResult(True)
        if client is None:
            return CommandResult(False, "no active media player")
        if not self._current.can_control:
            return CommandResult(False, "active media player does not accept controls")
        try:
            async with asyncio.timeout(2.0):
                await self._execute(client, command, arguments)
        except TimeoutError:
            LOGGER.warning("MPRIS command %s timed out", command)
            return CommandResult(False, "media player command timed out")
        except (DBusError, RuntimeError, ValueError) as error:
            LOGGER.warning("MPRIS command %s failed: %s", command, error)
            return CommandResult(False, f"media player rejected command: {error}")
        self._wake_event.set()
        return CommandResult(True)

    async def _execute(
        self, client: _MPRISPlayer, command: str, arguments: dict[str, object]
    ) -> None:
        if command == "play":
            await client.player.call_play()
        elif command == "pause":
            await client.player.call_pause()
        elif command == "toggle":
            await client.player.call_play_pause()
        elif command == "previous":
            await client.player.call_previous()
        elif command == "next":
            await client.player.call_next()
        elif command == "seek":
            offset_ms = arguments.get("offset_ms")
            if not isinstance(offset_ms, int) or isinstance(offset_ms, bool):
                raise ValueError("seek offset is invalid")
            await client.player.call_seek(offset_ms * 1000)
        elif command in {"set_volume", "volume_up", "volume_down", "mute"}:
            await self._set_volume(client, command, arguments)
        elif command == "shuffle_toggle":
            if self._current.shuffle is None:
                raise ValueError("shuffle is not supported")
            await client.properties.call_set(
                PLAYER_INTERFACE, "Shuffle", Variant("b", not self._current.shuffle)
            )
        elif command == "set_repeat":
            loop_status = {"off": "None", "track": "Track", "playlist": "Playlist"}.get(
                str(arguments.get("mode"))
            )
            if loop_status is None or self._current.repeat is None:
                raise ValueError("repeat mode is not supported")
            await client.properties.call_set(
                PLAYER_INTERFACE, "LoopStatus", Variant("s", loop_status)
            )
        else:
            raise ValueError(f"unsupported command: {command}")

    async def _set_volume(
        self, client: _MPRISPlayer, command: str, arguments: dict[str, object]
    ) -> None:
        if self._current.volume is None:
            raise ValueError("volume is not supported")
        current = self._current.volume
        if command == "set_volume":
            value = arguments.get("value")
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValueError("volume is invalid")
            target = float(value)
        elif command == "volume_up":
            target = current + 0.05
        elif command == "volume_down":
            target = current - 0.05
        elif current > 0.0001:
            self._muted_restore_volume = current
            target = 0.0
        else:
            target = self._muted_restore_volume
        await client.properties.call_set(
            PLAYER_INTERFACE, "Volume", Variant("d", min(1.0, max(0.0, target)))
        )

    async def _connect_bus(self) -> None:
        bus = await asyncio.wait_for(MessageBus(bus_type=BusType.SESSION).connect(), timeout=2.0)
        introspection = await asyncio.wait_for(
            bus.introspect("org.freedesktop.DBus", "/org/freedesktop/DBus"), timeout=1.5
        )
        proxy = bus.get_proxy_object("org.freedesktop.DBus", "/org/freedesktop/DBus", introspection)
        self._bus = bus
        self._dbus = proxy.get_interface("org.freedesktop.DBus")
        self._last_scan = 0.0
        LOGGER.info("Connected to the session D-Bus")

    async def _poll(self) -> None:
        reconnect_delay = 1.0
        while not self._stop_event.is_set():
            try:
                if self._bus is None:
                    await self._connect_bus()
                    reconnect_delay = 1.0
                await self._poll_once()
            except asyncio.CancelledError:
                raise
            except Exception as error:  # boundary: D-Bus library exposes varied connection errors
                LOGGER.warning("MPRIS polling unavailable: %s", error)
                if self._bus is not None:
                    self._bus.disconnect()  # type: ignore[no-untyped-call]
                self._bus = None
                self._dbus = None
                self._players.clear()
                self._snapshots.clear()
                self._snapshot_failures.clear()
                await self._set_empty_state()
                await self._wait(reconnect_delay)
                reconnect_delay = min(30.0, reconnect_delay * 2)
                continue
            await self._wait(POLL_SECONDS)

    async def _wait(self, delay: float) -> None:
        self._wake_event.clear()
        try:
            await asyncio.wait_for(self._wake_event.wait(), timeout=delay)
        except TimeoutError:
            pass

    async def _poll_once(self) -> None:
        now = monotonic()
        if now - self._last_scan >= PLAYER_SCAN_SECONDS:
            names = await asyncio.wait_for(self._dbus.call_list_names(), timeout=1.5)
            player_ids = {str(name) for name in names if str(name).startswith(MPRIS_PREFIX)}
            for stale in set(self._players) - player_ids:
                self._players.pop(stale, None)
                self._snapshots.pop(stale, None)
                self._snapshot_failures.pop(stale, None)
            for new_player in player_ids - set(self._players):
                try:
                    self._players[new_player] = await _MPRISPlayer.connect(self._bus, new_player)  # type: ignore[arg-type]
                    LOGGER.info("Detected MPRIS player %s", new_player)
                except (
                    DBusError,
                    InterfaceNotFoundError,
                    OSError,
                    RuntimeError,
                    TimeoutError,
                ) as error:
                    LOGGER.debug("Could not inspect MPRIS player %s: %s", new_player, error)
            self._last_scan = now
        for player_id, player in list(self._players.items()):
            try:
                self._snapshots[player_id] = await player.snapshot()
                self._snapshot_failures.pop(player_id, None)
            except (DBusError, OSError, RuntimeError, TimeoutError) as error:
                LOGGER.debug("Could not refresh MPRIS player %s: %s", player_id, error)
                failures = self._snapshot_failures.get(player_id, 0) + 1
                self._snapshot_failures[player_id] = failures
                if failures >= MAX_SNAPSHOT_FAILURES:
                    self._snapshots.pop(player_id, None)
        summaries = [
            PlayerSummary(player_id, state.player_name or player_id, state.status)
            for player_id, state in self._snapshots.items()
        ]
        selected = select_active_player(
            summaries,
            previous_id=self._current.player_id,
            preferred_id=self._preferred_player,
        )
        if selected is None:
            await self._set_empty_state()
            return
        previous = self._current
        self._current = self._snapshots[selected.player_id]
        force = previous.content_key() != self._current.content_key()
        await self._publish(force=force)

    async def _set_empty_state(self) -> None:
        if (
            self._current.player_id is not None
            or self._current.content_key() != PlaybackState().content_key()
        ):
            self._current = PlaybackState()
            await self._publish(force=True)

    async def _publish(self, force: bool = False) -> None:
        now = monotonic()
        if not force and now - self._last_publish < POSITION_SYNC_SECONDS:
            return
        self._last_publish = now
        if self._callback is not None:
            await self._callback(self._current)
