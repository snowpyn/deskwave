from __future__ import annotations

import asyncio
import logging
from dataclasses import replace

import pytest
from conftest import FakeBackend

import deskwave_host.service.media as media_module
from deskwave_host.artwork import FALLBACK_THEME, ArtworkCache, ResolvedArtwork
from deskwave_host.config import HostConfig
from deskwave_host.models import PlaybackState, PlaybackStatus, ThemePalette
from deskwave_host.service import MediaService

THEME_A = ThemePalette(0x336699, 0x7799BB, 0x07111A, 0xF1F4F2)
THEME_B = ThemePalette(0xA84E61, 0xD68D72, 0x180A10, 0xF8F1F2)
THEME_C = ThemePalette(0x3A9164, 0x7BC18F, 0x07160F, 0xF0F7F2)
ASSET_A = ResolvedArtwork("a" * 64, THEME_A)
ASSET_B = ResolvedArtwork("b" * 64, THEME_B)
ASSET_C = ResolvedArtwork("c" * 64, THEME_C)


async def settle(rounds: int = 8) -> None:
    for _ in range(rounds):
        await asyncio.sleep(0)


class CountingArtworkCache(ArtworkCache):
    def __init__(self, config: HostConfig, result: ResolvedArtwork = ASSET_A) -> None:
        super().__init__(config)
        self.result = result
        self.calls: list[tuple[str, str, bool]] = []

    async def resolve(
        self,
        url: str,
        cache_variant: str = "",
        *,
        force_refresh: bool = False,
    ) -> ResolvedArtwork | None:
        self.calls.append((url, cache_variant, force_refresh))
        return self.result


async def test_position_and_pause_updates_retain_atomic_visual_tuple(
    host_config: HostConfig,
) -> None:
    initial = PlaybackState(
        track_id="track-1",
        artwork_url="file:///cover.jpg",
        position_ms=1_000,
        status=PlaybackStatus.PLAYING,
    )
    backend = FakeBackend(initial)
    artwork = CountingArtworkCache(host_config)
    service = MediaService(backend, artwork)
    await service.start()
    await settle()
    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        ASSET_A.artwork_id,
        THEME_A,
        1,
    )

    await backend.emit(
        replace(initial, artwork_url=None, position_ms=3_000, status=PlaybackStatus.PAUSED)
    )
    await settle()

    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        ASSET_A.artwork_id,
        THEME_A,
        1,
    )
    assert len(artwork.calls) == 1
    await service.stop()


async def test_rapid_track_change_ignores_noncooperative_stale_completion(
    host_config: HostConfig,
    caplog: pytest.LogCaptureFixture,
) -> None:
    class DelayedArtworkCache(ArtworkCache):
        def __init__(self, config: HostConfig) -> None:
            super().__init__(config)
            self.old_started = asyncio.Event()
            self.release_old = asyncio.Event()

        async def resolve(
            self,
            url: str,
            cache_variant: str = "",
            *,
            force_refresh: bool = False,
        ) -> ResolvedArtwork | None:
            del cache_variant, force_refresh
            if url.endswith("old"):
                self.old_started.set()
                try:
                    await self.release_old.wait()
                except asyncio.CancelledError:
                    # Simulate an underlying worker that cannot stop immediately.
                    await self.release_old.wait()
                return ASSET_A
            return ASSET_B

    backend = FakeBackend(PlaybackState(track_id="old", artwork_url="https://example.com/old"))
    artwork = DelayedArtworkCache(host_config)
    service = MediaService(backend, artwork)
    caplog.set_level(logging.DEBUG, logger="service")
    await service.start()
    await artwork.old_started.wait()

    await backend.emit(PlaybackState(track_id="new", artwork_url="https://example.com/new"))
    await settle()
    artwork.release_old.set()
    await settle()

    assert service.state.track_id == "new"
    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        ASSET_B.artwork_id,
        THEME_B,
        2,
    )
    assert "Rejected stale artwork generation 1 at post-resolve (context=" in caplog.text
    assert "example.com" not in caplog.text
    assert "https://example.com/old" not in caplog.text
    await service.stop()


async def test_reused_artwork_url_uses_distinct_track_cache_variants(
    host_config: HostConfig,
) -> None:
    class SequencedArtworkCache(CountingArtworkCache):
        async def resolve(
            self,
            url: str,
            cache_variant: str = "",
            *,
            force_refresh: bool = False,
        ) -> ResolvedArtwork | None:
            self.calls.append((url, cache_variant, force_refresh))
            return ASSET_A if len(self.calls) == 1 else ASSET_B

    shared_url = "https://example.com/player-current-cover.jpg"
    initial = PlaybackState(track_id="track-old", artwork_url=shared_url)
    backend = FakeBackend(initial)
    artwork = SequencedArtworkCache(host_config)
    service = MediaService(backend, artwork)
    await service.start()
    await settle()
    await backend.emit(PlaybackState(track_id="track-new", artwork_url=shared_url))
    await settle()

    assert len(artwork.calls) == 2
    assert artwork.calls[0][1] != artwork.calls[1][1]
    assert artwork.calls[0][2] is False
    assert artwork.calls[1][2] is False
    assert service.state.artwork_id == ASSET_B.artwork_id
    assert service.state.theme == THEME_B
    assert service.state.artwork_generation == 2
    await service.stop()


async def test_reused_track_id_and_art_url_are_disambiguated_by_metadata(
    host_config: HostConfig,
) -> None:
    shared_url = "https://example.com/player-current-cover.jpg"
    initial = PlaybackState(
        player_id="org.mpris.MediaPlayer2.constant",
        track_id="/constant-track",
        title="Old title",
        artists=("Old artist",),
        artwork_url=shared_url,
    )
    backend = FakeBackend(initial)
    artwork = CountingArtworkCache(host_config, ASSET_A)
    service = MediaService(backend, artwork)
    await service.start()
    await settle()

    await backend.emit(
        replace(
            initial,
            title="New title",
            artists=(),
            artwork_url=None,
        )
    )
    await settle()
    assert (service.state.artwork_id, service.state.artwork_generation) == (
        ASSET_A.artwork_id,
        1,
    )

    artwork.result = ASSET_B
    await backend.emit(
        replace(
            initial,
            title="New title",
            artists=("New artist",),
            artwork_url=shared_url,
        )
    )
    await settle()

    assert len(artwork.calls) == 2
    assert artwork.calls[0][1] != artwork.calls[1][1]
    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        ASSET_B.artwork_id,
        THEME_B,
        2,
    )
    await service.stop()


async def test_metadata_addition_after_visual_promotion_disambiguates_reused_identity(
    host_config: HostConfig,
) -> None:
    shared_url = "https://example.com/player-current-cover.jpg"
    initial = PlaybackState(
        player_id="org.mpris.MediaPlayer2.constant",
        track_id="/constant-track",
        title="",
        artists=("Same artist",),
        album="Same album",
        artwork_url=shared_url,
    )
    backend = FakeBackend(initial)
    artwork = CountingArtworkCache(host_config, ASSET_A)
    service = MediaService(backend, artwork)
    await service.start()
    await settle()
    assert service.state.artwork_generation == 1

    artwork.result = ASSET_B
    await backend.emit(replace(initial, title="New title"))
    await settle()

    assert len(artwork.calls) == 2
    assert artwork.calls[0][1] != artwork.calls[1][1]
    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        ASSET_B.artwork_id,
        THEME_B,
        2,
    )
    await service.stop()


async def test_metadata_addition_while_visual_is_pending_keeps_the_same_identity(
    host_config: HostConfig,
) -> None:
    class DelayedArtworkCache(ArtworkCache):
        def __init__(self, config: HostConfig) -> None:
            super().__init__(config)
            self.calls = 0
            self.started = asyncio.Event()
            self.release = asyncio.Event()

        async def resolve(
            self,
            url: str,
            cache_variant: str = "",
            *,
            force_refresh: bool = False,
        ) -> ResolvedArtwork | None:
            del url, cache_variant, force_refresh
            self.calls += 1
            self.started.set()
            await self.release.wait()
            return ASSET_A

    initial = PlaybackState(
        player_id="org.mpris.MediaPlayer2.constant",
        track_id="/constant-track",
        title="",
        artists=("Same artist",),
        album="Same album",
        artwork_url="https://example.com/player-current-cover.jpg",
    )
    backend = FakeBackend(initial)
    artwork = DelayedArtworkCache(host_config)
    service = MediaService(backend, artwork)
    await service.start()
    await artwork.started.wait()

    await backend.emit(replace(initial, title="Delayed title"))
    await settle()
    artwork.release.set()
    await settle()

    assert artwork.calls == 1
    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        ASSET_A.artwork_id,
        THEME_A,
        1,
    )
    await service.stop()


async def test_same_track_id_carries_identity_through_an_empty_metadata_gap(
    host_config: HostConfig,
) -> None:
    initial = PlaybackState(
        player_id="org.mpris.MediaPlayer2.constant",
        track_id="/constant-track",
        title="Stable title",
        artists=("Stable artist",),
        album="Stable album",
        artwork_url="file:///cover.jpg",
    )
    backend = FakeBackend(initial)
    artwork = CountingArtworkCache(host_config, ASSET_A)
    service = MediaService(backend, artwork)
    await service.start()
    await settle()

    await backend.emit(
        PlaybackState(
            player_id=initial.player_id,
            track_id=initial.track_id,
            artwork_url=None,
        )
    )
    await settle()

    assert len(artwork.calls) == 1
    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        ASSET_A.artwork_id,
        THEME_A,
        1,
    )
    await service.stop()


async def test_one_backend_update_drives_bounded_immediate_retries(
    host_config: HostConfig, monkeypatch: pytest.MonkeyPatch
) -> None:
    class FlakyArtworkCache(ArtworkCache):
        def __init__(self, config: HostConfig) -> None:
            super().__init__(config)
            self.calls = 0

        async def resolve(
            self,
            url: str,
            cache_variant: str = "",
            *,
            force_refresh: bool = False,
        ) -> ResolvedArtwork | None:
            del url, cache_variant, force_refresh
            self.calls += 1
            return ASSET_C if self.calls == 3 else None

    monkeypatch.setattr(media_module, "ARTWORK_RETRY_BACKOFF_SECONDS", (0.0, 0.0))
    backend = FakeBackend(
        PlaybackState(track_id="track-1", artwork_url="file:///tmp/current-cover.jpg")
    )
    artwork = FlakyArtworkCache(host_config)
    service = MediaService(backend, artwork)
    await service.start()
    await settle(12)

    assert artwork.calls == 3
    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        ASSET_C.artwork_id,
        THEME_C,
        1,
    )
    await service.stop()


async def test_retry_exhaustion_promotes_deliberate_fallback(
    host_config: HostConfig, monkeypatch: pytest.MonkeyPatch
) -> None:
    class FailingArtworkCache(ArtworkCache):
        def __init__(self, config: HostConfig) -> None:
            super().__init__(config)
            self.calls = 0

        async def resolve(
            self,
            url: str,
            cache_variant: str = "",
            *,
            force_refresh: bool = False,
        ) -> ResolvedArtwork | None:
            del url, cache_variant, force_refresh
            self.calls += 1
            return None

    monkeypatch.setattr(media_module, "ARTWORK_RETRY_BACKOFF_SECONDS", (0.0, 0.0))
    backend = FakeBackend(
        PlaybackState(track_id="track-1", artwork_url="https://example.com/missing")
    )
    artwork = FailingArtworkCache(host_config)
    service = MediaService(backend, artwork)
    await service.start()
    await settle(12)

    assert artwork.calls == media_module.ARTWORK_MAX_ATTEMPTS
    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        None,
        FALLBACK_THEME,
        1,
    )
    await service.stop()


async def test_delayed_art_url_keeps_previous_visual_then_promotes_new_bundle(
    host_config: HostConfig, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(media_module, "ARTWORK_METADATA_GRACE_SECONDS", 60.0)
    backend = FakeBackend(PlaybackState(track_id="old", artwork_url="file:///old.jpg"))
    artwork = CountingArtworkCache(host_config, ASSET_A)
    service = MediaService(backend, artwork)
    await service.start()
    await settle()
    assert service.state.artwork_generation == 1

    await backend.emit(PlaybackState(track_id="new", title="New", artwork_url=None))
    await settle()
    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        ASSET_A.artwork_id,
        THEME_A,
        1,
    )

    artwork.result = ASSET_B
    await backend.emit(PlaybackState(track_id="new", title="New", artwork_url="file:///new.jpg"))
    await settle()

    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        ASSET_B.artwork_id,
        THEME_B,
        2,
    )
    await service.stop()


async def test_authoritative_no_art_metadata_grace_promotes_fallback(
    host_config: HostConfig, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(media_module, "ARTWORK_METADATA_GRACE_SECONDS", 0.0)
    backend = FakeBackend(PlaybackState(track_id="old", artwork_url="file:///old.jpg"))
    artwork = CountingArtworkCache(host_config)
    service = MediaService(backend, artwork)
    await service.start()
    await settle()

    await backend.emit(PlaybackState(track_id="new", title="No cover"))
    await settle()

    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        None,
        FALLBACK_THEME,
        2,
    )
    await service.stop()


async def test_empty_reconnect_snapshot_keeps_promoted_visual_tuple(
    host_config: HostConfig,
) -> None:
    backend = FakeBackend(PlaybackState(track_id="track-1", artwork_url="file:///cover.jpg"))
    artwork = CountingArtworkCache(host_config)
    service = MediaService(backend, artwork)
    await service.start()
    await settle()

    await backend.emit(PlaybackState())
    await settle()

    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        ASSET_A.artwork_id,
        THEME_A,
        1,
    )
    assert len(artwork.calls) == 1
    await service.stop()


async def test_retry_exhaustion_during_reconnect_keeps_previous_visual_and_rearms(
    host_config: HostConfig, monkeypatch: pytest.MonkeyPatch
) -> None:
    class ReconnectingArtworkCache(ArtworkCache):
        def __init__(self, config: HostConfig) -> None:
            super().__init__(config)
            self.calls = 0
            self.failure_started = asyncio.Event()
            self.release_failure = asyncio.Event()
            self.recovered = False

        async def resolve(
            self,
            url: str,
            cache_variant: str = "",
            *,
            force_refresh: bool = False,
        ) -> ResolvedArtwork | None:
            del url, cache_variant, force_refresh
            self.calls += 1
            if self.calls == 1:
                return ASSET_A
            if self.recovered:
                return ASSET_B
            self.failure_started.set()
            await self.release_failure.wait()
            return None

    monkeypatch.setattr(media_module, "ARTWORK_RETRY_BACKOFF_SECONDS", (0.0, 0.0))
    old = PlaybackState(track_id="old", artwork_url="file:///old.jpg")
    new = PlaybackState(track_id="new", artwork_url="https://example.com/new.jpg")
    backend = FakeBackend(old)
    artwork = ReconnectingArtworkCache(host_config)
    service = MediaService(backend, artwork)
    await service.start()
    await settle()

    await backend.emit(new)
    await artwork.failure_started.wait()
    await backend.emit(PlaybackState())
    artwork.release_failure.set()
    await settle(16)

    assert artwork.calls == 1 + media_module.ARTWORK_MAX_ATTEMPTS
    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        ASSET_A.artwork_id,
        THEME_A,
        1,
    )

    artwork.recovered = True
    await backend.emit(new)
    await settle()

    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        ASSET_B.artwork_id,
        THEME_B,
        2,
    )
    await service.stop()


async def test_empty_snapshot_suspends_no_art_grace_until_track_returns(
    host_config: HostConfig, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(media_module, "ARTWORK_METADATA_GRACE_SECONDS", 0.0)
    backend = FakeBackend(PlaybackState(track_id="old", artwork_url="file:///cover.jpg"))
    artwork = CountingArtworkCache(host_config)
    service = MediaService(backend, artwork)
    await service.start()
    await settle()

    pending = PlaybackState(track_id="new", title="No art yet")
    await backend.emit(pending)
    await backend.emit(PlaybackState())
    await settle()

    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        ASSET_A.artwork_id,
        THEME_A,
        1,
    )

    await backend.emit(pending)
    await settle()

    assert (service.state.artwork_id, service.state.theme, service.state.artwork_generation) == (
        None,
        FALLBACK_THEME,
        2,
    )
    await service.stop()
