"""DeskWave Host command-line entry point."""

from __future__ import annotations

import argparse
from collections.abc import Sequence

from deskwave_host import __version__


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="deskwave-host")
    parser.add_argument("--version", action="version", version=f"DeskWave Host {__version__}")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    build_parser().parse_args(argv)
    return 0
