from __future__ import annotations

from pathlib import Path
from socket import AF_INET

import pytest
from PIL import Image

import deskwave_host.artwork as artwork_module
from deskwave_host.artwork import ArtworkCache, _SafeResolver
from deskwave_host.config import HostConfig


def make_image(path: Path, size: tuple[int, int] = (640, 480)) -> None:
    image = Image.new("RGB", size, color=(30, 90, 180))
    image.save(path, format="PNG")


async def test_local_artwork_is_resized_cached_and_served(
    host_config: HostConfig, tmp_path: Path
) -> None:
    source = tmp_path / "cover.png"
    make_image(source)
    cache = ArtworkCache(host_config)
    artwork_id = await cache.resolve(source.as_uri())
    assert artwork_id is not None
    assert await cache.resolve(source.as_uri()) == artwork_id
    assert cache._locks == {}
    rendered = cache.path_for(artwork_id)
    assert rendered is not None
    with Image.open(rendered) as image:
        assert image.size == (320, 320)
        assert image.format == "JPEG"


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
