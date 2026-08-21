#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path

import tomllib

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    version = (ROOT / "VERSION").read_text(encoding="ascii").strip()
    if re.fullmatch(r"\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?", version) is None:
        print("VERSION is not valid semantic version text", file=sys.stderr)
        return 1
    with (ROOT / "host/pyproject.toml").open("rb") as stream:
        host_version = tomllib.load(stream)["project"]["version"]
    package_source = (ROOT / "host/src/deskwave_host/__init__.py").read_text(
        encoding="utf-8"
    )
    fallback = re.search(r'__version__ = "([^"]+)"', package_source)
    values = {
        "VERSION": version,
        "host/pyproject.toml": str(host_version),
        "host package fallback": fallback.group(1) if fallback else "<missing>",
    }
    mismatches = {name: value for name, value in values.items() if value != version}
    if mismatches:
        for name, value in mismatches.items():
            print(f"{name} has {value!r}, expected {version!r}", file=sys.stderr)
        return 1
    print(f"DeskWave version sources agree: {version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
