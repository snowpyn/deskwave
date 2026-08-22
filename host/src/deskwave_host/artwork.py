"""Bounded, cached artwork retrieval and ESP32-friendly transformation."""

from __future__ import annotations

import asyncio
import hashlib
import ipaddress
import logging
import os
import socket
import tempfile
from io import BytesIO
from pathlib import Path
from time import time
from urllib.parse import ParseResult, unquote, urljoin, urlparse

import aiohttp
from aiohttp.abc import AbstractResolver, ResolveResult
from aiohttp.resolver import DefaultResolver
from PIL import Image, ImageOps, UnidentifiedImageError

from deskwave_host.config import HostConfig

LOGGER = logging.getLogger("artwork")
ARTWORK_SIZE = (320, 320)
ARTWORK_FORMAT_VERSION = "320x320-jpeg-q92-444"
MAX_IMAGE_PIXELS = 20_000_000
HTTP_CACHE_SECONDS = 24 * 60 * 60
MAX_REDIRECTS = 3
MAX_SOURCE_MAPPINGS = 2_048
Image.MAX_IMAGE_PIXELS = MAX_IMAGE_PIXELS


class ArtworkError(ValueError):
    """Artwork could not be retrieved or decoded safely."""


class _SafeResolver(AbstractResolver):
    """Resolve once and validate the exact addresses given to aiohttp."""

    def __init__(self, allow_private: bool) -> None:
        self._allow_private = allow_private
        self._delegate = DefaultResolver()

    async def resolve(
        self,
        host: str,
        port: int = 0,
        family: socket.AddressFamily = socket.AF_INET,
    ) -> list[ResolveResult]:
        results = await self._delegate.resolve(host, port, family)
        if not results:
            raise OSError("artwork host did not resolve")
        if not self._allow_private:
            for result in results:
                try:
                    address = ipaddress.ip_address(result["host"].split("%", 1)[0])
                except ValueError as error:
                    raise OSError("artwork host resolved to an invalid address") from error
                if not address.is_global:
                    raise OSError("private or special-purpose artwork hosts are disabled")
        return results

    async def close(self) -> None:
        await self._delegate.close()


class ArtworkCache:
    def __init__(self, config: HostConfig) -> None:
        self._config = config
        self._directory = config.paths.cache_dir / "artwork"
        self._mapping_directory = self._directory / "sources"
        self._locks: dict[str, asyncio.Lock] = {}
        self._lock_users: dict[str, int] = {}
        self._cache_lock = asyncio.Lock()

    async def resolve(self, url: str) -> str | None:
        try:
            source_key = await self._source_key(url)
            mapping_path = (
                self._mapping_directory / f"{hashlib.sha256(source_key.encode()).hexdigest()}.map"
            )
            cached = self._read_mapping(mapping_path, url)
            if cached is not None:
                return cached
            lock_key = mapping_path.stem
            lock = self._locks.setdefault(lock_key, asyncio.Lock())
            self._lock_users[lock_key] = self._lock_users.get(lock_key, 0) + 1
            try:
                async with lock:
                    cached = self._read_mapping(mapping_path, url)
                    if cached is not None:
                        return cached
                    raw = await self._fetch(url)
                    rendered = await asyncio.to_thread(self._transform, raw)
                    artwork_id = hashlib.sha256(rendered).hexdigest()
                    async with self._cache_lock:
                        await asyncio.to_thread(self._store, artwork_id, rendered, mapping_path)
                        await asyncio.to_thread(self._evict)
                    return artwork_id
            finally:
                users = self._lock_users[lock_key] - 1
                if users == 0:
                    self._lock_users.pop(lock_key, None)
                    if self._locks.get(lock_key) is lock:
                        self._locks.pop(lock_key, None)
                else:
                    self._lock_users[lock_key] = users
        except (ArtworkError, OSError, ValueError, aiohttp.ClientError, TimeoutError) as error:
            LOGGER.warning("Artwork unavailable for %s: %s", self._redact_url(url), error)
            return None

    def path_for(self, artwork_id: str) -> Path | None:
        if len(artwork_id) != 64 or any(
            character not in "0123456789abcdef" for character in artwork_id
        ):
            return None
        path = self._directory / f"{artwork_id}.jpg"
        return path if path.is_file() else None

    async def _source_key(self, url: str) -> str:
        parsed = urlparse(url)
        if parsed.scheme == "file":
            path = self._file_path(parsed)
            stat = await asyncio.to_thread(path.stat)
            if not path.is_file() or stat.st_size > self._config.artwork_max_bytes:
                raise ArtworkError("local artwork is not a bounded regular file")
            return f"{ARTWORK_FORMAT_VERSION}\0{url}\0{stat.st_mtime_ns}\0{stat.st_size}"
        if parsed.scheme in {"http", "https"}:
            return f"{ARTWORK_FORMAT_VERSION}\0{url}"
        raise ArtworkError("only file, http, and https artwork URLs are supported")

    def _read_mapping(self, path: Path, url: str) -> str | None:
        try:
            if urlparse(url).scheme in {"http", "https"}:
                if time() - path.stat().st_mtime > HTTP_CACHE_SECONDS:
                    return None
            artwork_id = path.read_text(encoding="ascii").strip()
            artwork_path = self.path_for(artwork_id)
            if artwork_path is not None:
                os.utime(artwork_path, None)
                os.utime(path, None)
                return artwork_id
        except (FileNotFoundError, OSError, UnicodeError):
            return None
        return None

    async def _fetch(self, url: str) -> bytes:
        parsed = urlparse(url)
        if parsed.scheme == "file":
            path = self._file_path(parsed)
            return await asyncio.to_thread(self._read_bounded_file, path)
        if parsed.scheme in {"http", "https"}:
            return await self._fetch_http(url)
        raise ArtworkError("unsupported artwork URL")

    def _file_path(self, parsed: ParseResult) -> Path:
        hostname = parsed.hostname
        if hostname not in {None, "", "localhost"}:
            raise ArtworkError("remote file URLs are not supported")
        path = Path(unquote(parsed.path))
        if not path.is_absolute():
            raise ArtworkError("file artwork path must be absolute")
        return path

    def _read_bounded_file(self, path: Path) -> bytes:
        if not path.is_file():
            raise ArtworkError("local artwork is not a regular file")
        with path.open("rb") as stream:
            data = stream.read(self._config.artwork_max_bytes + 1)
        if len(data) > self._config.artwork_max_bytes:
            raise ArtworkError("local artwork exceeds the size limit")
        return data

    async def _fetch_http(self, url: str) -> bytes:
        timeout = aiohttp.ClientTimeout(total=8.0, connect=3.0, sock_read=4.0)
        current = url
        resolver = _SafeResolver(self._config.allow_private_artwork_hosts)
        connector = aiohttp.TCPConnector(resolver=resolver)
        try:
            async with aiohttp.ClientSession(
                timeout=timeout, auto_decompress=False, connector=connector
            ) as session:
                for redirect_count in range(MAX_REDIRECTS + 1):
                    self._validate_remote_url(current)
                    async with session.get(
                        current,
                        allow_redirects=False,
                        headers={"User-Agent": "DeskWave-Host/0.1.0", "Accept": "image/*"},
                    ) as response:
                        if response.status in {301, 302, 303, 307, 308}:
                            if redirect_count == MAX_REDIRECTS:
                                raise ArtworkError("artwork redirected too many times")
                            location = response.headers.get("Location")
                            if not location:
                                raise ArtworkError("artwork redirect omitted Location")
                            current = urljoin(current, location)
                            continue
                        if response.status != 200:
                            raise ArtworkError(f"artwork server returned HTTP {response.status}")
                        content_type = response.headers.get("Content-Type", "").split(";", 1)[0]
                        if not content_type.startswith("image/"):
                            raise ArtworkError("artwork response is not an image")
                        length = response.content_length
                        if length is not None and length > self._config.artwork_max_bytes:
                            raise ArtworkError("remote artwork exceeds the size limit")
                        chunks: list[bytes] = []
                        received = 0
                        async for chunk in response.content.iter_chunked(65_536):
                            received += len(chunk)
                            if received > self._config.artwork_max_bytes:
                                raise ArtworkError("remote artwork exceeds the size limit")
                            chunks.append(chunk)
                        return b"".join(chunks)
        finally:
            await resolver.close()
        raise ArtworkError("artwork redirect handling failed")

    def _validate_remote_url(self, url: str) -> None:
        try:
            parsed = urlparse(url)
            _ = parsed.port
        except ValueError as error:
            raise ArtworkError("artwork URL is malformed") from error
        if parsed.scheme not in {"http", "https"} or not parsed.hostname:
            raise ArtworkError("artwork URL is malformed")
        if parsed.username is not None or parsed.password is not None:
            raise ArtworkError("artwork URL credentials are not allowed")
        try:
            address = ipaddress.ip_address(parsed.hostname.split("%", 1)[0])
        except ValueError:
            return
        if not self._config.allow_private_artwork_hosts and not address.is_global:
            raise ArtworkError("private or special-purpose artwork hosts are disabled")

    def _transform(self, raw: bytes) -> bytes:
        if not raw:
            raise ArtworkError("artwork is empty")
        try:
            with Image.open(BytesIO(raw)) as source:
                if source.width * source.height > MAX_IMAGE_PIXELS:
                    raise ArtworkError("artwork exceeds the pixel limit")
                source.verify()
            with Image.open(BytesIO(raw)) as source:
                image = ImageOps.exif_transpose(source).convert("RGB")
                image = ImageOps.fit(image, ARTWORK_SIZE, method=Image.Resampling.LANCZOS)
                output = BytesIO()
                image.save(
                    output,
                    format="JPEG",
                    quality=92,
                    subsampling=0,
                    optimize=True,
                    progressive=False,
                )
                return output.getvalue()
        except (UnidentifiedImageError, Image.DecompressionBombError, OSError, ValueError) as error:
            raise ArtworkError("artwork is malformed or unsupported") from error

    def _store(self, artwork_id: str, data: bytes, mapping_path: Path) -> None:
        self._directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        self._mapping_directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        artwork_path = self._directory / f"{artwork_id}.jpg"
        if not artwork_path.exists():
            self._atomic_write(artwork_path, data)
        self._atomic_write(mapping_path, artwork_id.encode("ascii"))

    def _atomic_write(self, destination: Path, data: bytes) -> None:
        descriptor, temporary_name = tempfile.mkstemp(prefix=".tmp-", dir=destination.parent)
        temporary = Path(temporary_name)
        try:
            os.fchmod(descriptor, 0o600)
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            temporary.replace(destination)
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise

    def _evict(self) -> None:
        files = sorted(self._directory.glob("*.jpg"), key=lambda path: path.stat().st_mtime)
        total = sum(path.stat().st_size for path in files)
        for path in files:
            if total <= self._config.artwork_cache_bytes:
                break
            size = path.stat().st_size
            path.unlink(missing_ok=True)
            total -= size
        mappings = sorted(
            self._mapping_directory.glob("*.map"), key=lambda path: path.stat().st_mtime
        )
        retained_mappings: list[Path] = []
        for path in mappings:
            try:
                artwork_id = path.read_text(encoding="ascii").strip()
            except (OSError, UnicodeError):
                path.unlink(missing_ok=True)
                continue
            if self.path_for(artwork_id) is None:
                path.unlink(missing_ok=True)
            else:
                retained_mappings.append(path)
        for path in retained_mappings[:-MAX_SOURCE_MAPPINGS]:
            path.unlink(missing_ok=True)

    @staticmethod
    def _redact_url(url: str) -> str:
        try:
            parsed = urlparse(url)
        except ValueError:
            return "<invalid artwork URL>"
        if parsed.scheme == "file":
            return "file://<local artwork>"
        return f"{parsed.scheme}://{parsed.hostname or '<invalid>'}/…"
