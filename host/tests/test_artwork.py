from __future__ import annotations

from pathlib import Path

from PIL import Image

from deskwave_host.artwork import ArtworkCache
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
    rendered = cache.path_for(artwork_id)
    assert rendered is not None
    with Image.open(rendered) as image:
        assert image.size == (240, 240)
        assert image.format == "JPEG"


async def test_malformed_artwork_fails_closed(host_config: HostConfig, tmp_path: Path) -> None:
    source = tmp_path / "not-an-image.jpg"
    source.write_bytes(b"not an image")
    assert await ArtworkCache(host_config).resolve(source.as_uri()) is None


def test_path_lookup_rejects_traversal(host_config: HostConfig) -> None:
    assert ArtworkCache(host_config).path_for("../secrets") is None
