"""Bounded LRCLIB synchronized-lyrics lookup and persistent caching."""

from __future__ import annotations

import asyncio
import hashlib
import json
import logging
import os
import re
import tempfile
from bisect import bisect_right
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import aiohttp

from deskwave_host import __version__
from deskwave_host.config import HostConfig
from deskwave_host.models import (
    MAX_LYRIC_WINDOW_LINES,
    LyricLine,
    LyricsStatus,
    PlaybackState,
)

LOGGER = logging.getLogger("lyrics")
LRCLIB_ENDPOINT = "https://lrclib.net/api/get"
LRCLIB_SEARCH_ENDPOINT = "https://lrclib.net/api/search"
LYRICS_CACHE_SCHEMA = 1
MAX_LYRICS_RESPONSE_BYTES = 512 * 1024
MAX_LYRICS_CACHE_BYTES = 768 * 1024
MAX_LYRIC_LINES = 1_000
_TIMESTAMP = re.compile(r"\[(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?\]")


class LyricsError(ValueError):
    """A remote or cached lyrics document could not be used safely."""


class LyricsTransientError(LyricsError):
    """LRCLIB is temporarily unavailable and the lookup can be retried."""


@dataclass(frozen=True, slots=True)
class LyricsDocument:
    status: LyricsStatus
    lines: tuple[LyricLine, ...] = ()

    def window(self, position_ms: int) -> tuple[LyricLine, ...]:
        """Return a five-line window centered on the active timestamp."""

        if self.status is not LyricsStatus.SYNCED or not self.lines:
            return ()
        starts = [line.time_ms for line in self.lines]
        current = bisect_right(starts, max(0, position_ms)) - 1
        if current < 0:
            return self.lines[:MAX_LYRIC_WINDOW_LINES]
        start = max(0, current - 2)
        end = min(len(self.lines), start + MAX_LYRIC_WINDOW_LINES)
        start = max(0, end - MAX_LYRIC_WINDOW_LINES)
        return self.lines[start:end]


def _clean_text(value: str) -> str:
    return "".join(character if ord(character) >= 0x20 else " " for character in value).strip()


def parse_lrc(value: str) -> tuple[LyricLine, ...]:
    """Parse enhanced or classic LRC timestamps into sorted, bounded lines."""

    parsed: dict[int, str] = {}
    for raw_line in value.splitlines():
        matches = tuple(_TIMESTAMP.finditer(raw_line))
        if not matches:
            continue
        text = _clean_text(raw_line[matches[-1].end() :])[:512]
        if not text:
            continue
        for match in matches:
            minutes = int(match.group(1))
            seconds = int(match.group(2))
            if seconds >= 60:
                continue
            fraction = match.group(3) or "0"
            fraction_ms = int(fraction.ljust(3, "0")[:3])
            start_ms = (minutes * 60 + seconds) * 1_000 + fraction_ms
            if start_ms <= 7 * 24 * 60 * 60 * 1000:
                parsed[start_ms] = text
            if len(parsed) >= MAX_LYRIC_LINES:
                break
        if len(parsed) >= MAX_LYRIC_LINES:
            break
    return tuple(LyricLine(time_ms, parsed[time_ms]) for time_ms in sorted(parsed))


class LyricsCache:
    """Resolve one track through LRCLIB while deduplicating and caching lookups."""

    def __init__(self, config: HostConfig) -> None:
        self._enabled = config.lyrics_enabled
        self._directory = config.paths.cache_dir / "lyrics"
        self._locks: dict[str, asyncio.Lock] = {}
        self._lock_users: dict[str, int] = {}

    async def resolve(self, state: PlaybackState) -> LyricsDocument:
        if (
            not self._enabled
            or not state.title.strip()
            or not any(artist.strip() for artist in state.artists)
        ):
            return LyricsDocument(LyricsStatus.UNAVAILABLE)
        key = self._cache_key(state)
        cached = await asyncio.to_thread(self._read_cached, key)
        if cached is not None:
            LOGGER.debug("Lyrics cache hit for track %.12s", key)
            return cached
        lock = self._locks.setdefault(key, asyncio.Lock())
        self._lock_users[key] = self._lock_users.get(key, 0) + 1
        try:
            async with lock:
                cached = await asyncio.to_thread(self._read_cached, key)
                if cached is not None:
                    return cached
                try:
                    result = await self._fetch(state)
                except (aiohttp.ClientError, LyricsError, OSError, TimeoutError) as error:
                    LOGGER.warning(
                        "Lyrics lookup failed for track %.12s: %s", key, self._error_summary(error)
                    )
                    if isinstance(error, LyricsError):
                        raise
                    raise LyricsTransientError(self._error_summary(error)) from error
                if result.status in (LyricsStatus.SYNCED, LyricsStatus.INSTRUMENTAL):
                    try:
                        await asyncio.to_thread(self._store, key, result)
                    except (LyricsError, OSError) as error:
                        LOGGER.warning(
                            "Lyrics cache write failed for track %.12s: %s",
                            key,
                            self._error_summary(error),
                        )
                return result
        finally:
            remaining = self._lock_users.get(key, 1) - 1
            if remaining == 0:
                self._lock_users.pop(key, None)
                if self._locks.get(key) is lock:
                    self._locks.pop(key, None)
            else:
                self._lock_users[key] = remaining

    @staticmethod
    def _cache_key(state: PlaybackState) -> str:
        identity = json.dumps(
            {
                "title": state.title.strip().casefold(),
                "artists": [artist.strip().casefold() for artist in state.artists if artist],
                "album": state.album.strip().casefold(),
                "duration_ms": state.duration_ms,
            },
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        )
        return hashlib.sha256(identity.encode("utf-8")).hexdigest()

    def _cache_path(self, key: str) -> Path:
        return self._directory / f"{key}.json"

    def _read_cached(self, key: str) -> LyricsDocument | None:
        path = self._cache_path(key)
        try:
            if not path.is_file() or path.stat().st_size > MAX_LYRICS_CACHE_BYTES:
                return None
            document = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(document, dict) or document.get("schema") != LYRICS_CACHE_SCHEMA:
                return None
            status_value = document.get("status")
            if not isinstance(status_value, str):
                return None
            status = LyricsStatus(status_value)
            # Negative cache entries from earlier versions must not permanently
            # hide lyrics that become available or match through the search fallback.
            if status not in (LyricsStatus.SYNCED, LyricsStatus.INSTRUMENTAL):
                return None
            raw_lines = document.get("lines", [])
            if not isinstance(raw_lines, list) or len(raw_lines) > MAX_LYRIC_LINES:
                return None
            lines: list[LyricLine] = []
            for value in raw_lines:
                if not isinstance(value, dict):
                    return None
                time_ms = value.get("time_ms")
                text = value.get("text")
                if (
                    isinstance(time_ms, bool)
                    or not isinstance(time_ms, int)
                    or not 0 <= time_ms <= 7 * 24 * 60 * 60 * 1000
                    or not isinstance(text, str)
                ):
                    return None
                cleaned = _clean_text(text)[:512]
                if cleaned:
                    lines.append(LyricLine(time_ms, cleaned))
            if status is LyricsStatus.SYNCED and not lines:
                return None
            return LyricsDocument(status, tuple(lines))
        except (OSError, UnicodeError, json.JSONDecodeError, ValueError):
            return None

    def _store(self, key: str, result: LyricsDocument) -> None:
        self._directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        self._directory.chmod(0o700)
        payload = json.dumps(
            {
                "schema": LYRICS_CACHE_SCHEMA,
                "status": result.status.value,
                "lines": [{"time_ms": line.time_ms, "text": line.text} for line in result.lines],
            },
            ensure_ascii=False,
            separators=(",", ":"),
        ).encode("utf-8")
        if len(payload) > MAX_LYRICS_CACHE_BYTES:
            raise LyricsError("lyrics cache entry exceeds its size limit")
        descriptor, temporary_name = tempfile.mkstemp(prefix=f".{key}.", dir=self._directory)
        try:
            os.fchmod(descriptor, 0o600)
            with os.fdopen(descriptor, "wb") as stream:
                descriptor = -1
                stream.write(payload)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary_name, self._cache_path(key))
        finally:
            if descriptor >= 0:
                os.close(descriptor)
            try:
                os.unlink(temporary_name)
            except FileNotFoundError:
                pass

    async def _fetch(self, state: PlaybackState) -> LyricsDocument:
        exact_params = {
            "track_name": state.title,
            "artist_name": ", ".join(state.artists[:3]),
        }
        if state.album:
            exact_params["album_name"] = state.album
        if state.duration_ms is not None:
            exact_params["duration"] = f"{state.duration_ms / 1000:.3f}"
        search_params = {
            "track_name": state.title,
            "artist_name": ", ".join(state.artists[:3]),
        }
        free_search_params = {
            "q": " ".join((state.title, *state.artists[:3])),
        }
        # Lyrics are a secondary decoration. Bound a bad remote response tightly
        # so it cannot make a song change look stalled.
        timeout = aiohttp.ClientTimeout(total=3.0, connect=1.0, sock_read=2.0)
        headers = {
            "Accept": "application/json",
            "User-Agent": f"DeskWave/{__version__} (https://github.com/snowpyn/deskwave)",
        }
        async with aiohttp.ClientSession(timeout=timeout, headers=headers) as session:
            exact_error: LyricsError | None = None
            try:
                exact = await self._request_json(session, LRCLIB_ENDPOINT, exact_params)
            except LyricsTransientError:
                # A server/transport failure is shared by the exact and search
                # endpoints; avoid spending two more timeouts on redundant fallbacks.
                raise
            except LyricsError as error:
                # LRCLIB occasionally serves an HTML proxy/error body with HTTP 200.
                # A broken exact-match response says nothing about the independent
                # search endpoint, so keep going before reporting a lookup failure.
                exact = None
                exact_error = error
                LOGGER.info("Exact lyrics lookup failed; trying search fallback: %s", error)
            if exact is not None:
                if not isinstance(exact, dict):
                    raise LyricsError("lyrics response has an invalid shape")
                result = self._record_document(exact)
                if result is not None:
                    return result
            try:
                search = await self._request_json(session, LRCLIB_SEARCH_ENDPOINT, search_params)
            except LyricsTransientError:
                raise
            except LyricsError as structured_error:
                # Some releases and punctuation-heavy metadata fail LRCLIB's
                # structured search parser while its documented q search remains
                # usable. Make one bounded final request before surfacing failure.
                LOGGER.info(
                    "Structured lyrics search failed; trying free-text fallback: %s",
                    structured_error,
                )
                try:
                    search = await self._request_json(
                        session,
                        LRCLIB_SEARCH_ENDPOINT,
                        free_search_params,
                    )
                except LyricsError as fallback_error:
                    if isinstance(fallback_error, LyricsTransientError):
                        raise
                    if exact_error is not None:
                        raise structured_error from None
                    raise
        if search is None:
            return LyricsDocument(LyricsStatus.UNAVAILABLE)
        if not isinstance(search, list) or len(search) > 20:
            raise LyricsError("lyrics search response has an invalid shape")
        return self._select_search_result(search, state)

    async def _request_json(
        self,
        session: aiohttp.ClientSession,
        endpoint: str,
        params: dict[str, str],
    ) -> Any | None:
        async with session.get(endpoint, params=params, allow_redirects=False) as response:
            if response.status == 404:
                return None
            if response.status == 429 or response.status >= 500:
                raise LyricsTransientError(f"LRCLIB returned HTTP {response.status}")
            if response.status != 200:
                raise LyricsError(f"LRCLIB returned HTTP {response.status}")
            content_length = response.content_length
            if content_length is not None and content_length > MAX_LYRICS_RESPONSE_BYTES:
                raise LyricsError("lyrics response exceeds its size limit")
            raw = await response.content.read(MAX_LYRICS_RESPONSE_BYTES + 1)
        if len(raw) > MAX_LYRICS_RESPONSE_BYTES:
            raise LyricsError("lyrics response exceeds its size limit")
        try:
            return json.loads(raw)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise LyricsError("lyrics response is not valid JSON") from error

    @staticmethod
    def _record_document(document: dict[str, Any]) -> LyricsDocument | None:
        if document.get("instrumental") is True:
            return LyricsDocument(LyricsStatus.INSTRUMENTAL)
        synced = document.get("syncedLyrics")
        if not isinstance(synced, str) or not synced.strip():
            return None
        lines = parse_lrc(synced)
        if not lines:
            return None
        return LyricsDocument(LyricsStatus.SYNCED, lines)

    @classmethod
    def _select_search_result(
        cls,
        records: list[Any],
        state: PlaybackState,
    ) -> LyricsDocument:
        best: tuple[int, LyricsDocument] | None = None
        for record in records:
            if not isinstance(record, dict):
                continue
            document = cls._record_document(record)
            if document is None:
                continue
            score = cls._record_score(record, state)
            if score is None:
                continue
            if document.status is LyricsStatus.SYNCED:
                score += 20
            if best is None or score > best[0]:
                best = (score, document)
        return best[1] if best is not None else LyricsDocument(LyricsStatus.UNAVAILABLE)

    @staticmethod
    def _match_text(value: str) -> str:
        return "".join(character for character in value.casefold() if character.isalnum())

    @classmethod
    def _record_score(cls, record: dict[str, Any], state: PlaybackState) -> int | None:
        track_name = record.get("trackName", record.get("name"))
        artist_name = record.get("artistName")
        if not isinstance(track_name, str) or not isinstance(artist_name, str):
            return None
        wanted_track = cls._match_text(state.title)
        candidate_track = cls._match_text(track_name)
        wanted_artist = cls._match_text(" ".join(state.artists[:3]))
        candidate_artist = cls._match_text(artist_name)
        if not wanted_track or not candidate_track or not wanted_artist or not candidate_artist:
            return None
        if candidate_track == wanted_track:
            score = 100
        elif candidate_track in wanted_track or wanted_track in candidate_track:
            score = 70
        else:
            return None
        if candidate_artist == wanted_artist:
            score += 60
        elif candidate_artist in wanted_artist or wanted_artist in candidate_artist:
            score += 40
        else:
            return None
        album_name = record.get("albumName")
        if state.album and isinstance(album_name, str):
            if cls._match_text(album_name) == cls._match_text(state.album):
                score += 15
        duration = record.get("duration")
        if (
            state.duration_ms is not None
            and isinstance(duration, (int, float))
            and not isinstance(duration, bool)
        ):
            difference = abs(float(duration) - state.duration_ms / 1_000)
            if difference > 15.0:
                return None
            score += max(0, 15 - int(difference))
        return score

    @staticmethod
    def _error_summary(error: BaseException) -> str:
        if isinstance(error, LyricsError):
            return str(error)
        if isinstance(error, TimeoutError):
            return "request timed out"
        if isinstance(error, aiohttp.ClientError):
            return type(error).__name__
        if isinstance(error, OSError):
            return type(error).__name__
        return "unexpected lookup failure"
