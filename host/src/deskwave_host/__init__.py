"""DeskWave Host package."""

from importlib.metadata import PackageNotFoundError, version

try:
    __version__ = version("deskwave-host")
except PackageNotFoundError:  # pragma: no cover - source tree without install
    __version__ = "0.1.0"

__all__ = ["__version__"]
