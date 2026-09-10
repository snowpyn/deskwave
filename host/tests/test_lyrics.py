from __future__ import annotations

from typing import Any

import aiohttp

from deskwave_host.config import HostConfig
from deskwave_host.lyrics import (
    LRCLIB_ENDPOINT,
    LRCLIB_SEARCH_ENDPOINT,
    LyricsCache,
    LyricsDocument,
    LyricsError,
    parse_lrc,
)
from deskwave_host.models import LyricLine, LyricsStatus, PlaybackState


def test_parse_lrc_sorts_timestamps_and_ignores_metadata() -> None:
    lines = parse_lrc(
        "[ar:DeskWave]\n"
        "[00:12.340]Second line\n"
        "[00:03.50][00:08.5]First refrain\n"
        "[00:61.00]Invalid timestamp\n"
        "untimed copy\n"
    )

    assert lines == (
        LyricLine(3_500, "First refrain"),
        LyricLine(8_500, "First refrain"),
        LyricLine(12_340, "Second line"),
    )


def test_lyrics_document_centers_a_bounded_window_on_playback() -> None:
    document = LyricsDocument(
        LyricsStatus.SYNCED,
        tuple(LyricLine(index * 1_000, f"Line {index}") for index in range(10)),
    )

    assert [line.text for line in document.window(5_500)] == [
        "Line 3",
        "Line 4",
        "Line 5",
        "Line 6",
        "Line 7",
    ]
    assert [line.text for line in document.window(250)] == [
        "Line 0",
        "Line 1",
        "Line 2",
        "Line 3",
        "Line 4",
    ]


class StubLyricsCache(LyricsCache):
    def __init__(self, config: HostConfig, result: LyricsDocument) -> None:
        super().__init__(config)
        self.result = result
        self.calls = 0

    async def _fetch(self, state: PlaybackState) -> LyricsDocument:
        self.calls += 1
        return self.result


async def test_lyrics_lookup_persists_and_reuses_cache(host_config: HostConfig) -> None:
    result = LyricsDocument(
        LyricsStatus.SYNCED,
        (LyricLine(1_000, "A line"), LyricLine(2_000, "Another line")),
    )
    state = PlaybackState(
        title="A Track",
        artists=("An Artist",),
        album="An Album",
        duration_ms=180_000,
    )
    first = StubLyricsCache(host_config, result)
    second = StubLyricsCache(host_config, LyricsDocument(LyricsStatus.UNAVAILABLE))

    assert await first.resolve(state) == result
    assert await second.resolve(state) == result
    assert first.calls == 1
    assert second.calls == 0


async def test_lyrics_lookup_requires_title_and_artist(host_config: HostConfig) -> None:
    cache = StubLyricsCache(
        host_config,
        LyricsDocument(LyricsStatus.SYNCED, (LyricLine(0, "Should not be fetched"),)),
    )

    assert (await cache.resolve(PlaybackState(title="A Track"))).status is LyricsStatus.UNAVAILABLE
    assert (
        await cache.resolve(PlaybackState(title="A Track", artists=("   ",)))
    ).status is LyricsStatus.UNAVAILABLE
    assert cache.calls == 0


async def test_unavailable_lookup_is_not_persisted(host_config: HostConfig) -> None:
    state = PlaybackState(
        title="A Track",
        artists=("An Artist",),
        album="An Album",
        duration_ms=180_000,
    )
    cache = StubLyricsCache(host_config, LyricsDocument(LyricsStatus.UNAVAILABLE))

    assert (await cache.resolve(state)).status is LyricsStatus.UNAVAILABLE
    cache.result = LyricsDocument(
        LyricsStatus.SYNCED,
        (LyricLine(1_000, "Found after the first miss"),),
    )

    assert (await cache.resolve(state)).status is LyricsStatus.SYNCED
    assert cache.calls == 2


async def test_legacy_negative_cache_entry_is_ignored(host_config: HostConfig) -> None:
    state = PlaybackState(
        title="A Track",
        artists=("An Artist",),
        album="An Album",
        duration_ms=180_000,
    )
    cache = StubLyricsCache(
        host_config,
        LyricsDocument(LyricsStatus.SYNCED, (LyricLine(1_000, "Fresh line"),)),
    )
    key = cache._cache_key(state)
    cache._store(key, LyricsDocument(LyricsStatus.UNAVAILABLE))

    assert (await cache.resolve(state)).status is LyricsStatus.SYNCED
    assert cache.calls == 1


async def test_exact_miss_falls_back_to_best_structured_search_match(
    host_config: HostConfig,
) -> None:
    class RemoteStubLyricsCache(LyricsCache):
        def __init__(self, config: HostConfig) -> None:
            super().__init__(config)
            self.endpoints: list[str] = []

        async def _request_json(
            self,
            session: aiohttp.ClientSession,
            endpoint: str,
            params: dict[str, str],
        ) -> Any | None:
            del session, params
            self.endpoints.append(endpoint)
            if endpoint == LRCLIB_ENDPOINT:
                return None
            return [
                {
                    "trackName": "Drowning - Avicii Remix",
                    "artistName": "Armin van Buuren feat. Laura V",
                    "albumName": "Wrong Version",
                    "duration": 420.0,
                    "instrumental": False,
                    "syncedLyrics": "[00:01.00]Wrong duration",
                },
                {
                    "trackName": "Drowning - Avicii Remix",
                    "artistName": "Armin van Buuren feat. Laura V",
                    "albumName": "Mirage Remixes",
                    "duration": 472.0,
                    "instrumental": False,
                    "syncedLyrics": "[00:01.00]Matched fallback",
                },
            ]

    cache = RemoteStubLyricsCache(host_config)
    result = await cache.resolve(
        PlaybackState(
            title="Drowning (Avicii Remix)",
            artists=("Armin van Buuren feat. Laura V",),
            album="A State Of Trance Classics 14",
            duration_ms=473_000,
        )
    )

    assert result == LyricsDocument(LyricsStatus.SYNCED, (LyricLine(1_000, "Matched fallback"),))
    assert cache.endpoints == [LRCLIB_ENDPOINT, LRCLIB_SEARCH_ENDPOINT]


async def test_malformed_exact_response_still_uses_search_fallback(
    host_config: HostConfig,
) -> None:
    class RemoteStubLyricsCache(LyricsCache):
        def __init__(self, config: HostConfig) -> None:
            super().__init__(config)
            self.endpoints: list[str] = []

        async def _request_json(
            self,
            session: aiohttp.ClientSession,
            endpoint: str,
            params: dict[str, str],
        ) -> Any | None:
            del session, params
            self.endpoints.append(endpoint)
            if endpoint == LRCLIB_ENDPOINT:
                raise LyricsError("lyrics response is not valid JSON")
            return [
                {
                    "trackName": "A Track",
                    "artistName": "An Artist",
                    "albumName": "An Album",
                    "duration": 180.0,
                    "instrumental": False,
                    "syncedLyrics": "[00:01.00]Recovered through search",
                }
            ]

    cache = RemoteStubLyricsCache(host_config)
    result = await cache.resolve(
        PlaybackState(
            title="A Track",
            artists=("An Artist",),
            album="An Album",
            duration_ms=180_000,
        )
    )

    assert result == LyricsDocument(
        LyricsStatus.SYNCED,
        (LyricLine(1_000, "Recovered through search"),),
    )
    assert cache.endpoints == [LRCLIB_ENDPOINT, LRCLIB_SEARCH_ENDPOINT]


async def test_malformed_structured_search_uses_free_text_fallback(
    host_config: HostConfig,
) -> None:
    class RemoteStubLyricsCache(LyricsCache):
        def __init__(self, config: HostConfig) -> None:
            super().__init__(config)
            self.requests: list[tuple[str, dict[str, str]]] = []

        async def _request_json(
            self,
            session: aiohttp.ClientSession,
            endpoint: str,
            params: dict[str, str],
        ) -> Any | None:
            del session
            self.requests.append((endpoint, params))
            if endpoint == LRCLIB_ENDPOINT:
                return None
            if "q" not in params:
                raise LyricsError("lyrics response is not valid JSON")
            return [
                {
                    "trackName": "A Track (Live)",
                    "artistName": "An Artist",
                    "albumName": "An Album",
                    "duration": 180.0,
                    "instrumental": False,
                    "syncedLyrics": "[00:01.00]Recovered through free text",
                }
            ]

    cache = RemoteStubLyricsCache(host_config)
    result = await cache.resolve(
        PlaybackState(
            title="A Track (Live)",
            artists=("An Artist",),
            album="An Album",
            duration_ms=180_000,
        )
    )

    assert result == LyricsDocument(
        LyricsStatus.SYNCED,
        (LyricLine(1_000, "Recovered through free text"),),
    )
    assert [endpoint for endpoint, _ in cache.requests] == [
        LRCLIB_ENDPOINT,
        LRCLIB_SEARCH_ENDPOINT,
        LRCLIB_SEARCH_ENDPOINT,
    ]
    assert "q" in cache.requests[-1][1]
