"""Host configuration and XDG path management."""

from __future__ import annotations

import os
import tomllib
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any


class ConfigError(ValueError):
    """Configuration cannot be used safely."""


@dataclass(frozen=True, slots=True)
class HostPaths:
    config_dir: Path
    cache_dir: Path
    state_dir: Path

    @classmethod
    def discover(cls) -> HostPaths:
        home = Path.home()
        config_home = Path(os.environ.get("XDG_CONFIG_HOME", home / ".config"))
        cache_home = Path(os.environ.get("XDG_CACHE_HOME", home / ".cache"))
        state_home = Path(os.environ.get("XDG_STATE_HOME", home / ".local/state"))
        return cls(
            config_dir=config_home / "deskwave",
            cache_dir=cache_home / "deskwave",
            state_dir=state_home / "deskwave",
        )

    def ensure(self) -> None:
        for directory in (self.config_dir, self.cache_dir, self.state_dir):
            directory.mkdir(mode=0o700, parents=True, exist_ok=True)
            directory.chmod(0o700)


@dataclass(frozen=True, slots=True)
class HostConfig:
    # LAN devices must reach the service; all non-pairing routes authenticate.
    bind: str = "0.0.0.0"  # noqa: S104
    port: int = 8765
    service_name: str = "DeskWave Host"
    preferred_player: str | None = None
    log_level: str = "INFO"
    allow_private_artwork_hosts: bool = False
    artwork_max_bytes: int = 8 * 1024 * 1024
    artwork_cache_bytes: int = 128 * 1024 * 1024
    paths: HostPaths = HostPaths(Path(), Path(), Path())

    @classmethod
    def load(cls, paths: HostPaths | None = None) -> HostConfig:
        resolved_paths = paths or HostPaths.discover()
        config = cls(paths=resolved_paths)
        config_path = resolved_paths.config_dir / "config.toml"
        if config_path.exists():
            try:
                with config_path.open("rb") as stream:
                    document = tomllib.load(stream)
            except (OSError, tomllib.TOMLDecodeError) as error:
                raise ConfigError(f"cannot read {config_path}: {error}") from error
            if not isinstance(document, dict) or not isinstance(document.get("host", {}), dict):
                raise ConfigError("config.toml [host] must be a table")
            config = config._apply(document.get("host", {}))
        config = config._apply_environment()
        config.validate()
        return config

    def _apply(self, values: dict[str, Any]) -> HostConfig:
        allowed = {
            "bind",
            "port",
            "service_name",
            "preferred_player",
            "log_level",
            "allow_private_artwork_hosts",
            "artwork_max_bytes",
            "artwork_cache_bytes",
        }
        unknown = set(values) - allowed
        if unknown:
            raise ConfigError(f"unknown host configuration keys: {', '.join(sorted(unknown))}")
        return replace(self, **values)

    def _apply_environment(self) -> HostConfig:
        values: dict[str, Any] = {}
        environment = os.environ
        if "DESKWAVE_BIND" in environment:
            values["bind"] = environment["DESKWAVE_BIND"]
        if "DESKWAVE_PORT" in environment:
            try:
                values["port"] = int(environment["DESKWAVE_PORT"])
            except ValueError as error:
                raise ConfigError("DESKWAVE_PORT must be an integer") from error
        if "DESKWAVE_LOG_LEVEL" in environment:
            values["log_level"] = environment["DESKWAVE_LOG_LEVEL"].upper()
        if "DESKWAVE_PREFERRED_PLAYER" in environment:
            values["preferred_player"] = environment["DESKWAVE_PREFERRED_PLAYER"] or None
        return self._apply(values)

    def validate(self) -> None:
        if not isinstance(self.bind, str) or not self.bind or len(self.bind) > 255:
            raise ConfigError("host.bind must be a non-empty address")
        if (
            not isinstance(self.port, int)
            or isinstance(self.port, bool)
            or not 1 <= self.port <= 65535
        ):
            raise ConfigError("host.port must be between 1 and 65535")
        if self.log_level not in {"DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"}:
            raise ConfigError("host.log_level is invalid")
        if not isinstance(self.allow_private_artwork_hosts, bool):
            raise ConfigError("host.allow_private_artwork_hosts must be a boolean")
        if not 65_536 <= self.artwork_max_bytes <= 50_000_000:
            raise ConfigError("host.artwork_max_bytes is outside the safe range")
        if not 1_000_000 <= self.artwork_cache_bytes <= 2_000_000_000:
            raise ConfigError("host.artwork_cache_bytes is outside the safe range")
