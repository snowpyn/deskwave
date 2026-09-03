from __future__ import annotations

import json
import logging
from dataclasses import replace
from io import BytesIO
from pathlib import Path
from socket import AF_INET

import aiohttp
import pytest
from PIL import Image

import deskwave_host.artwork as artwork_module
from deskwave_host.artwork import (
    PALETTE_SCHEMA_VERSION,
    ArtworkCache,
    ResolvedArtwork,
    _contrast_ratio,
    _SafeResolver,
    _unpacked_rgb,
)
from deskwave_host.config import HostConfig
from deskwave_host.models import ThemePalette


def make_image(path: Path, size: tuple[int, int] = (640, 480)) -> None:
    image = Image.new("RGB", size, color=(30, 90, 180))
    image.save(path, format="PNG")


def image_bytes(color: tuple[int, int, int] = (30, 90, 180)) -> bytes:
    output = BytesIO()
    Image.new("RGB", (640, 480), color=color).save(output, format="PNG")
    return output.getvalue()


async def test_local_artwork_is_resized_cached_and_served(
    host_config: HostConfig, tmp_path: Path
) -> None:
    source = tmp_path / "cover.png"
    make_image(source)
    cache = ArtworkCache(host_config)
    resolved = await cache.resolve(source.as_uri())
    assert resolved is not None
    assert await cache.resolve(source.as_uri()) == resolved
    assert cache._locks == {}
    rendered = cache.path_for(resolved.artwork_id)
    assert rendered is not None
    with Image.open(rendered) as image:
        assert image.size == (320, 320)
        assert image.format == "JPEG"
    sidecar = cache._palette_path(resolved.artwork_id)
    document = json.loads(sidecar.read_text(encoding="ascii"))
    assert document == {
        "artwork_id": resolved.artwork_id,
        "theme": resolved.theme.to_payload(),
        "version": PALETTE_SCHEMA_VERSION,
    }


async def test_malformed_artwork_fails_closed(host_config: HostConfig, tmp_path: Path) -> None:
    source = tmp_path / "not-an-image.jpg"
    source.write_bytes(b"not an image")
    assert await ArtworkCache(host_config).resolve(source.as_uri()) is None


def test_path_lookup_rejects_traversal(host_config: HostConfig) -> None:
    assert ArtworkCache(host_config).path_for("../secrets") is None


async def test_private_and_malformed_http_urls_fail_closed(host_config: HostConfig) -> None:
    cache = ArtworkCache(host_config)
    assert await cache.resolve("http://127.0.0.1/cover.jpg") is None
    assert await cache.resolve("http://example.com:not-a-port/cover.jpg") is None


async def test_artwork_failure_logs_do_not_expose_source_paths_or_queries(
    host_config: HostConfig,
    tmp_path: Path,
    caplog: pytest.LogCaptureFixture,
) -> None:
    missing = tmp_path / "private-library" / "secret-cover.jpg"
    caplog.set_level(logging.WARNING, logger="artwork")

    assert await ArtworkCache(host_config).resolve(missing.as_uri()) is None

    assert str(missing) not in caplog.text
    assert "file://<local artwork>" in caplog.text
    caplog.clear()

    class FailingRemoteArtworkCache(ArtworkCache):
        async def _fetch(self, url: str) -> bytes:
            raise aiohttp.ClientConnectionError(f"could not retrieve {url}")

    remote_url = "https://example.com/private/cover.jpg?access_token=secret"
    assert await FailingRemoteArtworkCache(host_config).resolve(remote_url, "track") is None

    assert "/private/cover.jpg" not in caplog.text
    assert "access_token" not in caplog.text
    assert "secret" not in caplog.text
    assert "ClientConnectionError" in caplog.text


async def test_resolver_rejects_private_address_returned_at_connect_time() -> None:
    class PrivateResolver:
        async def resolve(self, host: str, port: int, family: object) -> list[dict[str, object]]:
            return [
                {
                    "hostname": host,
                    "host": "127.0.0.1",
                    "port": port,
                    "family": family,
                    "proto": 6,
                    "flags": 0,
                }
            ]

        async def close(self) -> None:
            return None

    resolver = _SafeResolver(allow_private=False)
    await resolver._delegate.close()
    resolver._delegate = PrivateResolver()  # type: ignore[assignment]
    with pytest.raises(OSError, match="private or special-purpose"):
        await resolver.resolve("rebind.example", 443, AF_INET)
    await resolver.close()


async def test_pixel_limit_is_enforced_before_decode(
    host_config: HostConfig, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    source = tmp_path / "oversized.png"
    make_image(source, size=(3, 2))
    monkeypatch.setattr(artwork_module, "MAX_IMAGE_PIXELS", 4)
    assert await ArtworkCache(host_config).resolve(source.as_uri()) is None


async def test_oversized_normalized_output_is_rejected_before_palette_or_store(
    host_config: HostConfig,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    cache = ArtworkCache(host_config)
    palette_calls = 0
    store_calls = 0

    async def fetch(_: str) -> bytes:
        return b"bounded source"

    def transform(_: bytes) -> bytes:
        return b"x" * (artwork_module.MAX_NORMALIZED_ARTWORK_BYTES + 1)

    def extract_palette(_: bytes) -> ThemePalette:
        nonlocal palette_calls
        palette_calls += 1
        return artwork_module.FALLBACK_THEME

    def store(
        artwork_id: str,
        data: bytes,
        theme: ThemePalette,
        mapping_path: Path,
    ) -> None:
        del artwork_id, data, theme, mapping_path
        nonlocal store_calls
        store_calls += 1

    monkeypatch.setattr(cache, "_fetch", fetch)
    monkeypatch.setattr(cache, "_transform", transform)
    monkeypatch.setattr(cache, "_extract_palette", extract_palette)
    monkeypatch.setattr(cache, "_store", store)

    resolved = await cache.resolve("https://example.com/oversized.jpg", "track")

    assert resolved is None
    assert palette_calls == 0
    assert store_calls == 0
    assert not cache._directory.exists()


async def test_palette_is_deterministic_and_contrast_safe(
    host_config: HostConfig, tmp_path: Path
) -> None:
    source = tmp_path / "split-cover.png"
    image = Image.new("RGB", (640, 480), color=(195, 35, 55))
    image.paste((30, 80, 205), (320, 0, 640, 480))
    image.save(source, format="PNG")

    first = await ArtworkCache(host_config).resolve(source.as_uri())
    second = await ArtworkCache(host_config).resolve(source.as_uri())

    assert first is not None
    assert second == first
    background = _unpacked_rgb(first.theme.background)
    assert _contrast_ratio(_unpacked_rgb(first.theme.primary), background) >= 3.0
    assert _contrast_ratio(_unpacked_rgb(first.theme.secondary), background) >= 3.0
    assert _contrast_ratio(_unpacked_rgb(first.theme.foreground), background) >= 4.5
    assert first.theme.primary != first.theme.secondary


@pytest.mark.parametrize("sidecar_contents", [None, b"{", b'{"version":999}'])
async def test_missing_or_corrupt_palette_sidecar_is_rebuilt(
    host_config: HostConfig,
    tmp_path: Path,
    sidecar_contents: bytes | None,
) -> None:
    source = tmp_path / "cover.png"
    make_image(source)
    cache = ArtworkCache(host_config)
    first = await cache.resolve(source.as_uri())
    assert first is not None
    sidecar = cache._palette_path(first.artwork_id)
    if sidecar_contents is None:
        sidecar.unlink()
    else:
        sidecar.write_bytes(sidecar_contents)

    rebuilt = await ArtworkCache(host_config).resolve(source.as_uri())

    assert rebuilt == first
    assert json.loads(sidecar.read_text(encoding="ascii"))["version"] == PALETTE_SCHEMA_VERSION


async def test_truncated_cached_jpeg_is_rejected_and_rebuilt(
    host_config: HostConfig, tmp_path: Path
) -> None:
    source = tmp_path / "cover.png"
    make_image(source)
    cache = ArtworkCache(host_config)
    first = await cache.resolve(source.as_uri())
    assert first is not None
    rendered = cache.path_for(first.artwork_id)
    assert rendered is not None
    rendered.write_bytes(b"\xff\xd8truncated")

    # A different track variant bypasses the original source mapping, so this
    # also verifies _store replaces an invalid pre-existing content object.
    rebuilt = await ArtworkCache(host_config).resolve(source.as_uri(), "new-track-variant")

    assert rebuilt == first
    repaired_path = cache.path_for(first.artwork_id)
    assert repaired_path is not None
    assert repaired_path.stat().st_size > len(b"\xff\xd8truncated")


async def test_track_variant_prevents_reused_http_mapping_staleness(
    host_config: HostConfig,
) -> None:
    class ChangingRemoteArtworkCache(ArtworkCache):
        def __init__(self, config: HostConfig) -> None:
            super().__init__(config)
            self.fetches = 0

        async def _fetch(self, url: str) -> bytes:
            assert url == "https://example.com/current-cover.jpg"
            self.fetches += 1
            color = (190, 40, 55) if self.fetches == 1 else (35, 75, 205)
            return image_bytes(color)

    cache = ChangingRemoteArtworkCache(host_config)
    url = "https://example.com/current-cover.jpg"

    first = await cache.resolve(url, "track-a")
    repeated = await cache.resolve(url, "track-a")
    second = await cache.resolve(url, "track-b")

    assert first is not None
    assert repeated == first
    assert second is not None
    assert second.artwork_id != first.artwork_id
    assert cache.fetches == 2


async def test_http_and_https_sources_use_the_bounded_image_pipeline(
    host_config: HostConfig,
) -> None:
    class StaticRemoteArtworkCache(ArtworkCache):
        async def _fetch(self, url: str) -> bytes:
            assert url.startswith(("http://", "https://"))
            return image_bytes((55, 145, 95))

    cache = StaticRemoteArtworkCache(host_config)
    http = await cache.resolve("http://example.com/cover.jpg", "http-track")
    https = await cache.resolve("https://example.com/cover.jpg", "https-track")

    assert isinstance(http, ResolvedArtwork)
    assert isinstance(https, ResolvedArtwork)
    assert http.artwork_id == https.artwork_id
    assert http.theme == https.theme


async def test_eviction_removes_palette_sidecar_with_jpeg(
    host_config: HostConfig, tmp_path: Path
) -> None:
    source = tmp_path / "cover.png"
    make_image(source)
    cache = ArtworkCache(host_config)
    resolved = await cache.resolve(source.as_uri())
    assert resolved is not None
    sidecar = cache._palette_path(resolved.artwork_id)
    assert sidecar.is_file()
    cache._config = replace(host_config, artwork_cache_bytes=1)

    cache._evict()

    assert not (cache._directory / f"{resolved.artwork_id}.jpg").exists()
    assert not sidecar.exists()
