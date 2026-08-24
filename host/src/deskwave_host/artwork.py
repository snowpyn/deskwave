"""Bounded, cached artwork retrieval and ESP32-friendly transformation."""

from __future__ import annotations

import asyncio
import colorsys
import hashlib
import ipaddress
import json
import logging
import os
import socket
import tempfile
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
from time import time
from typing import Any, TypeAlias
from urllib.parse import ParseResult, unquote, urljoin, urlparse

import aiohttp
from aiohttp.abc import AbstractResolver, ResolveResult
from aiohttp.resolver import DefaultResolver
from PIL import Image, ImageOps, UnidentifiedImageError

from deskwave_host.config import HostConfig
from deskwave_host.models import ThemePalette

LOGGER = logging.getLogger("artwork")
ARTWORK_SIZE = (320, 320)
ARTWORK_FORMAT_VERSION = "320x320-jpeg-q92-444"
MAX_IMAGE_PIXELS = 20_000_000
HTTP_CACHE_SECONDS = 24 * 60 * 60
MAX_REDIRECTS = 3
MAX_SOURCE_MAPPINGS = 2_048
MAX_NORMALIZED_ARTWORK_BYTES = 393_216
PALETTE_SCHEMA_VERSION = 1
PALETTE_SAMPLE_SIZE = (48, 48)
PALETTE_COLOR_COUNT = 12
PALETTE_MIN_ACCENT_CONTRAST = 3.0
PALETTE_MIN_FOREGROUND_CONTRAST = 4.5
MAX_PALETTE_SIDECAR_BYTES = 2_048
FALLBACK_THEME = ThemePalette(
    primary=0x6FDAC2,
    secondary=0xA891DE,
    background=0x070A12,
    foreground=0xF1F4F2,
)
Image.MAX_IMAGE_PIXELS = MAX_IMAGE_PIXELS

Rgb: TypeAlias = tuple[int, int, int]


class ArtworkError(ValueError):
    """Artwork could not be retrieved or decoded safely."""


def _safe_error_summary(error: BaseException) -> str:
    """Describe an artwork failure without echoing a source URL or local path."""

    if isinstance(error, ArtworkError):
        return str(error)
    if isinstance(error, TimeoutError):
        return "request timed out"
    if isinstance(error, aiohttp.ClientResponseError):
        return f"artwork server returned HTTP {error.status}"
    if isinstance(error, aiohttp.ClientError):
        return type(error).__name__
    if isinstance(error, OSError):
        detail = error.strerror
        return f"{type(error).__name__}: {detail}" if detail else type(error).__name__
    return type(error).__name__


@dataclass(frozen=True, slots=True)
class ResolvedArtwork:
    """One content-addressed JPEG and its validated derived palette."""

    artwork_id: str
    theme: ThemePalette


@dataclass(frozen=True, slots=True)
class _ColorCandidate:
    count: int
    rgb: Rgb
    saturation: float
    luminance: float


def _packed_rgb(rgb: Rgb) -> int:
    return (rgb[0] << 16) | (rgb[1] << 8) | rgb[2]


def _unpacked_rgb(value: int) -> Rgb:
    return (value >> 16, (value >> 8) & 0xFF, value & 0xFF)


def _blend_rgb(foreground: Rgb, background: Rgb, amount: float) -> Rgb:
    inverse = 1.0 - amount
    return tuple(
        max(0, min(255, round(foreground[index] * amount + background[index] * inverse)))
        for index in range(3)
    )  # type: ignore[return-value]


def _relative_luminance(rgb: Rgb) -> float:
    def linear(channel: int) -> float:
        value = channel / 255.0
        return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4

    return 0.2126 * linear(rgb[0]) + 0.7152 * linear(rgb[1]) + 0.0722 * linear(rgb[2])


def _contrast_ratio(first: Rgb, second: Rgb) -> float:
    first_luminance = _relative_luminance(first)
    second_luminance = _relative_luminance(second)
    lighter = max(first_luminance, second_luminance)
    darker = min(first_luminance, second_luminance)
    return (lighter + 0.05) / (darker + 0.05)


def _ensure_contrast(color: Rgb, background: Rgb, minimum: float) -> Rgb:
    if _contrast_ratio(color, background) >= minimum:
        return color
    for step in range(1, 21):
        candidate = _blend_rgb((255, 255, 255), color, step / 20.0)
        if _contrast_ratio(candidate, background) >= minimum:
            return candidate
    return (255, 255, 255)


def _require_packed_rgb(value: object) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFF_FFFF:
        raise ValueError("palette sidecar contains an invalid packed RGB value")
    return value


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

    async def resolve(
        self,
        url: str,
        cache_variant: str = "",
        *,
        force_refresh: bool = False,
    ) -> ResolvedArtwork | None:
        try:
            source_key = await self._source_key(url, cache_variant)
            mapping_path = (
                self._mapping_directory / f"{hashlib.sha256(source_key.encode()).hexdigest()}.map"
            )
            if not force_refresh:
                cached = await asyncio.to_thread(self._read_cached, mapping_path, url)
                if cached is not None:
                    LOGGER.debug(
                        "Artwork cache hit for %s as %.12s",
                        self._redact_url(url),
                        cached.artwork_id,
                    )
                    return cached
            else:
                LOGGER.debug(
                    "Refreshing artwork for %s (variant %.12s)",
                    self._redact_url(url),
                    hashlib.sha256(cache_variant.encode()).hexdigest(),
                )
            lock_key = mapping_path.stem
            lock = self._locks.setdefault(lock_key, asyncio.Lock())
            self._lock_users[lock_key] = self._lock_users.get(lock_key, 0) + 1
            try:
                async with lock:
                    if not force_refresh:
                        cached = await asyncio.to_thread(self._read_cached, mapping_path, url)
                        if cached is not None:
                            return cached
                    raw = await self._fetch(url)
                    rendered = await asyncio.to_thread(self._transform, raw)
                    if not 5 <= len(rendered) <= MAX_NORMALIZED_ARTWORK_BYTES:
                        raise ArtworkError("normalized artwork has an invalid size")
                    artwork_id = hashlib.sha256(rendered).hexdigest()
                    theme = await asyncio.to_thread(self._extract_palette, rendered)
                    async with self._cache_lock:
                        await asyncio.to_thread(
                            self._store,
                            artwork_id,
                            rendered,
                            theme,
                            mapping_path,
                        )
                        await asyncio.to_thread(self._evict)
                    LOGGER.debug(
                        "Prepared artwork %.12s and palette for %s",
                        artwork_id,
                        self._redact_url(url),
                    )
                    return ResolvedArtwork(artwork_id, theme)
            finally:
                users = self._lock_users[lock_key] - 1
                if users == 0:
                    self._lock_users.pop(lock_key, None)
                    if self._locks.get(lock_key) is lock:
                        self._locks.pop(lock_key, None)
                else:
                    self._lock_users[lock_key] = users
        except (ArtworkError, OSError, ValueError, aiohttp.ClientError, TimeoutError) as error:
            LOGGER.warning(
                "Artwork unavailable for %s: %s",
                self._redact_url(url),
                _safe_error_summary(error),
            )
            return None

    def path_for(self, artwork_id: str) -> Path | None:
        if len(artwork_id) != 64 or any(
            character not in "0123456789abcdef" for character in artwork_id
        ):
            return None
        path = self._directory / f"{artwork_id}.jpg"
        if not path.is_file():
            return None
        try:
            self._validate_cached_artwork(artwork_id, path)
        except (ArtworkError, OSError):
            LOGGER.debug("Discarding corrupt cached artwork %.12s", artwork_id)
            path.unlink(missing_ok=True)
            self._palette_path(artwork_id).unlink(missing_ok=True)
            return None
        return path

    def _validate_cached_artwork(self, artwork_id: str, path: Path) -> bytes:
        data = path.read_bytes()
        if not 5 <= len(data) <= MAX_NORMALIZED_ARTWORK_BYTES:
            raise ArtworkError("cached artwork has an invalid size")
        if hashlib.sha256(data).hexdigest() != artwork_id:
            raise ArtworkError("cached artwork does not match its content hash")
        try:
            with Image.open(BytesIO(data)) as image:
                if image.format != "JPEG" or image.size != ARTWORK_SIZE:
                    raise ArtworkError("cached artwork has an invalid format or size")
                image.load()
        except (UnidentifiedImageError, Image.DecompressionBombError, OSError, ValueError) as error:
            raise ArtworkError("cached artwork cannot be decoded") from error
        return data

    async def _source_key(self, url: str, cache_variant: str = "") -> str:
        parsed = urlparse(url)
        variant = hashlib.sha256(cache_variant.encode()).hexdigest() if cache_variant else ""
        if parsed.scheme == "file":
            path = self._file_path(parsed)
            stat = await asyncio.to_thread(path.stat)
            if not path.is_file() or stat.st_size > self._config.artwork_max_bytes:
                raise ArtworkError("local artwork is not a bounded regular file")
            return f"{ARTWORK_FORMAT_VERSION}\0{url}\0{stat.st_mtime_ns}\0{stat.st_size}\0{variant}"
        if parsed.scheme in {"http", "https"}:
            return f"{ARTWORK_FORMAT_VERSION}\0{url}\0{variant}"
        raise ArtworkError("only file, http, and https artwork URLs are supported")

    def _read_cached(self, path: Path, url: str) -> ResolvedArtwork | None:
        try:
            if urlparse(url).scheme in {"http", "https"}:
                if time() - path.stat().st_mtime > HTTP_CACHE_SECONDS:
                    return None
            artwork_id = path.read_text(encoding="ascii").strip()
            artwork_path = self.path_for(artwork_id)
            if artwork_path is not None:
                rendered = artwork_path.read_bytes()
                theme = self._load_or_rebuild_palette(artwork_id, rendered)
                os.utime(artwork_path, None)
                os.utime(self._palette_path(artwork_id), None)
                os.utime(path, None)
                return ResolvedArtwork(artwork_id, theme)
        except (ArtworkError, FileNotFoundError, OSError, UnicodeError, ValueError) as error:
            LOGGER.debug(
                "Discarding unusable cached artwork mapping for %s: %s",
                self._redact_url(url),
                error,
            )
            try:
                artwork_id = path.read_text(encoding="ascii").strip()
            except (OSError, UnicodeError):
                artwork_id = ""
            if len(artwork_id) == 64:
                artwork_path = self.path_for(artwork_id)
                if artwork_path is not None:
                    artwork_path.unlink(missing_ok=True)
                self._palette_path(artwork_id).unlink(missing_ok=True)
            path.unlink(missing_ok=True)
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

    def _extract_palette(self, rendered: bytes) -> ThemePalette:
        """Derive deterministic, contrast-safe colors from the final cached JPEG."""

        try:
            with Image.open(BytesIO(rendered)) as source:
                sample = source.convert("RGB").resize(
                    PALETTE_SAMPLE_SIZE,
                    resample=Image.Resampling.BOX,
                )
                quantized = sample.quantize(
                    colors=PALETTE_COLOR_COUNT,
                    method=Image.Quantize.MEDIANCUT,
                    dither=Image.Dither.NONE,
                ).convert("RGB")
                colors = quantized.getcolors(PALETTE_SAMPLE_SIZE[0] * PALETTE_SAMPLE_SIZE[1])
        except (UnidentifiedImageError, Image.DecompressionBombError, OSError, ValueError) as error:
            raise ArtworkError("normalized artwork cache object is malformed") from error

        if not colors:
            return FALLBACK_THEME

        candidates: list[_ColorCandidate] = []
        for count, color in colors:
            if not isinstance(color, tuple) or len(color) < 3:
                continue
            rgb = (int(color[0]), int(color[1]), int(color[2]))
            _, saturation, _ = colorsys.rgb_to_hsv(
                rgb[0] / 255.0,
                rgb[1] / 255.0,
                rgb[2] / 255.0,
            )
            candidates.append(
                _ColorCandidate(
                    count=int(count),
                    rgb=rgb,
                    saturation=saturation,
                    luminance=_relative_luminance(rgb),
                )
            )
        if not candidates:
            return FALLBACK_THEME

        dominant = max(
            candidates, key=lambda candidate: (candidate.count, -_packed_rgb(candidate.rgb))
        )
        fallback_background = _unpacked_rgb(FALLBACK_THEME.background)
        background = _blend_rgb(dominant.rgb, fallback_background, 0.18)
        if _relative_luminance(background) > 0.12:
            background = _blend_rgb(background, fallback_background, 0.65)

        def accent_score(candidate: _ColorCandidate) -> float:
            midpoint = max(0.25, 1.0 - abs(candidate.luminance - 0.48) * 1.5)
            return candidate.count * (0.35 + 0.65 * candidate.saturation) * midpoint

        primary_candidate = max(
            candidates,
            key=lambda candidate: (
                accent_score(candidate),
                candidate.count,
                -_packed_rgb(candidate.rgb),
            ),
        )
        primary = _ensure_contrast(
            primary_candidate.rgb,
            background,
            PALETTE_MIN_ACCENT_CONTRAST,
        )

        def normalized_distance(candidate: _ColorCandidate) -> float:
            squared = sum(
                (candidate.rgb[index] - primary_candidate.rgb[index]) ** 2 for index in range(3)
            )
            return squared / (3.0 * 255.0 * 255.0)

        secondary_candidates = [
            candidate for candidate in candidates if candidate.rgb != primary_candidate.rgb
        ]
        if secondary_candidates:
            secondary_candidate = max(
                secondary_candidates,
                key=lambda candidate: (
                    accent_score(candidate) * (0.35 + normalized_distance(candidate)),
                    candidate.count,
                    -_packed_rgb(candidate.rgb),
                ),
            )
            secondary_seed = secondary_candidate.rgb
            if normalized_distance(secondary_candidate) < 0.015:
                secondary_seed = _blend_rgb(
                    _unpacked_rgb(FALLBACK_THEME.secondary),
                    primary,
                    0.28,
                )
        else:
            secondary_seed = _blend_rgb(
                _unpacked_rgb(FALLBACK_THEME.secondary),
                primary,
                0.28,
            )
        secondary = _ensure_contrast(
            secondary_seed,
            background,
            PALETTE_MIN_ACCENT_CONTRAST,
        )

        light_foreground = _blend_rgb(dominant.rgb, _unpacked_rgb(FALLBACK_THEME.foreground), 0.04)
        dark_foreground = (7, 10, 18)
        foreground = max(
            (light_foreground, dark_foreground),
            key=lambda color: _contrast_ratio(color, background),
        )
        foreground = _ensure_contrast(
            foreground,
            background,
            PALETTE_MIN_FOREGROUND_CONTRAST,
        )
        return ThemePalette(
            primary=_packed_rgb(primary),
            secondary=_packed_rgb(secondary),
            background=_packed_rgb(background),
            foreground=_packed_rgb(foreground),
        )

    def _palette_path(self, artwork_id: str) -> Path:
        return self._directory / f"{artwork_id}.palette.json"

    def _read_palette_sidecar(self, artwork_id: str) -> ThemePalette | None:
        path = self._palette_path(artwork_id)
        try:
            raw = path.read_bytes()
            if not raw or len(raw) > MAX_PALETTE_SIDECAR_BYTES:
                return None
            document: Any = json.loads(raw)
            if (
                not isinstance(document, dict)
                or document.get("version") != PALETTE_SCHEMA_VERSION
                or document.get("artwork_id") != artwork_id
            ):
                return None
            theme = document.get("theme")
            if not isinstance(theme, dict):
                return None
            return ThemePalette(
                primary=_require_packed_rgb(theme.get("primary")),
                secondary=_require_packed_rgb(theme.get("secondary")),
                background=_require_packed_rgb(theme.get("background")),
                foreground=_require_packed_rgb(theme.get("foreground")),
            )
        except (OSError, UnicodeError, ValueError, TypeError):
            return None

    def _write_palette_sidecar(self, artwork_id: str, theme: ThemePalette) -> None:
        document = {
            "version": PALETTE_SCHEMA_VERSION,
            "artwork_id": artwork_id,
            "theme": theme.to_payload(),
        }
        encoded = json.dumps(document, sort_keys=True, separators=(",", ":")).encode("ascii")
        self._atomic_write(self._palette_path(artwork_id), encoded)

    def _load_or_rebuild_palette(self, artwork_id: str, rendered: bytes) -> ThemePalette:
        theme = self._read_palette_sidecar(artwork_id)
        if theme is not None:
            return theme
        theme = self._extract_palette(rendered)
        self._write_palette_sidecar(artwork_id, theme)
        LOGGER.debug("Rebuilt palette sidecar for artwork %.12s", artwork_id)
        return theme

    def _store(
        self,
        artwork_id: str,
        data: bytes,
        theme: ThemePalette,
        mapping_path: Path,
    ) -> None:
        self._directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        self._mapping_directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        artwork_path = self._directory / f"{artwork_id}.jpg"
        artwork_valid = False
        if artwork_path.is_file():
            try:
                self._validate_cached_artwork(artwork_id, artwork_path)
                artwork_valid = True
            except (ArtworkError, OSError):
                artwork_path.unlink(missing_ok=True)
                self._palette_path(artwork_id).unlink(missing_ok=True)
        if not artwork_valid:
            self._atomic_write(artwork_path, data)
        self._write_palette_sidecar(artwork_id, theme)
        # The source mapping is the commit marker: readers see it only after the
        # content-addressed JPEG and its palette sidecar are both durable.
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
            artwork_id = path.stem
            path.unlink(missing_ok=True)
            self._palette_path(artwork_id).unlink(missing_ok=True)
            total -= size
        for sidecar in self._directory.glob("*.palette.json"):
            artwork_id = sidecar.name.removesuffix(".palette.json")
            if not (self._directory / f"{artwork_id}.jpg").is_file():
                sidecar.unlink(missing_ok=True)
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
            if not (self._directory / f"{artwork_id}.jpg").is_file():
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
