from __future__ import annotations

import asyncio
from dataclasses import replace

from conftest import FakeBackend

from deskwave_host.artwork import ArtworkCache
from deskwave_host.config import HostConfig
from deskwave_host.models import PlaybackState
from deskwave_host.service import MediaService


class CountingArtworkCache(ArtworkCache):
    def __init__(self, config: HostConfig) -> None:
        super().__init__(config)
        self.calls = 0

    async def resolve(self, url: str) -> str | None:
        self.calls += 1
        return "a" * 64


async def test_position_updates_retain_resolved_artwork(host_config: HostConfig) -> None:
    initial = PlaybackState(
        track_id="track-1",
        artwork_url="file:///cover.jpg",
        position_ms=1_000,
    )
    backend = FakeBackend(initial)
    artwork = CountingArtworkCache(host_config)
    service = MediaService(backend, artwork)
    await service.start()
    await asyncio.sleep(0)
    assert service.state.artwork_id == "a" * 64

    await backend.emit(replace(initial, position_ms=3_000))
    await asyncio.sleep(0)
    assert service.state.artwork_id == "a" * 64
    assert artwork.calls == 1
    await service.stop()


async def test_changed_artwork_cancels_stale_resolution(host_config: HostConfig) -> None:
    class DelayedArtworkCache(ArtworkCache):
        def __init__(self, config: HostConfig) -> None:
            super().__init__(config)
            self.release = asyncio.Event()

        async def resolve(self, url: str) -> str | None:
            await self.release.wait()
            return ("a" if url.endswith("old") else "b") * 64

    backend = FakeBackend(PlaybackState(track_id="old", artwork_url="https://example.com/old"))
    artwork = DelayedArtworkCache(host_config)
    service = MediaService(backend, artwork)
    await service.start()
    await backend.emit(PlaybackState(track_id="new", artwork_url="https://example.com/new"))
    artwork.release.set()
    await asyncio.sleep(0)
    await asyncio.sleep(0)
    assert service.state.track_id == "new"
    assert service.state.artwork_id == "b" * 64
    await service.stop()
