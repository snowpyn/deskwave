"""DeskWave Host command-line interface and daemon entry point."""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import signal
import socket
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import Any

import aiohttp
from dbus_next.aio import MessageBus  # type: ignore[attr-defined]
from dbus_next.constants import BusType

from deskwave_host import __version__
from deskwave_host.api import serve
from deskwave_host.artwork import ArtworkCache
from deskwave_host.backends import MPRISBackend
from deskwave_host.config import ConfigError, HostConfig
from deskwave_host.discovery import discover_lan_address
from deskwave_host.service import MediaService
from deskwave_host.storage import DeviceStore, PairingError

LOGGER = logging.getLogger("cli")


def _configure_logging(level: str) -> None:
    logging.basicConfig(
        level=getattr(logging, level),
        format="[%(levelname)s][%(name)s] %(message)s",
    )


def _config() -> HostConfig:
    config = HostConfig.load()
    config.paths.ensure()
    return config


def _store(config: HostConfig) -> DeviceStore:
    return DeviceStore(config.paths.state_dir / "devices.sqlite3")


async def _run_daemon(config: HostConfig) -> int:
    _configure_logging(config.log_level)
    store = _store(config)
    artwork = ArtworkCache(config)
    backend = MPRISBackend(config.preferred_player)
    service = MediaService(backend, artwork)
    stop_event = asyncio.Event()
    loop = asyncio.get_running_loop()
    for signum in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(signum, stop_event.set)
    try:
        await serve(config, store, service, artwork, stop_event)
    finally:
        store.close()
    return 0


async def _fetch_health(config: HostConfig) -> dict[str, Any] | None:
    url = f"http://127.0.0.1:{config.port}/healthz"
    timeout = aiohttp.ClientTimeout(total=2.0)
    try:
        async with aiohttp.ClientSession(timeout=timeout) as session:
            async with session.get(url) as response:
                if response.status != 200:
                    return None
                document = await response.json()
                return document if isinstance(document, dict) else None
    except (aiohttp.ClientError, TimeoutError, json.JSONDecodeError):
        return None


async def _status(config: HostConfig) -> int:
    health = await _fetch_health(config)
    if health is None:
        print(f"DeskWave Host is not reachable on 127.0.0.1:{config.port}")
        return 3
    print(f"DeskWave Host {health.get('version', 'unknown')}: {health.get('status', 'unknown')}")
    print(f"Protocol: {health.get('protocol', 'unknown')}")
    print(f"Active player: {health.get('active_player') or 'none'}")
    return 0


def _format_time(timestamp: int | None) -> str:
    if timestamp is None:
        return "never"
    return datetime.fromtimestamp(timestamp, tz=UTC).astimezone().isoformat(timespec="seconds")


def _devices(config: HostConfig) -> int:
    store = _store(config)
    try:
        devices = store.list_devices()
    finally:
        store.close()
    if not devices:
        print("No paired DeskWave devices.")
        return 0
    for device in devices:
        print(f"{device.device_id}\t{device.name}\tlast seen: {_format_time(device.last_seen)}")
    return 0


def _pair(config: HostConfig, code: str | None) -> int:
    store = _store(config)
    try:
        if code is None:
            pending = store.list_pending()
            if not pending:
                print("No pending pairing requests. Start pairing on the DeskWave device first.")
                return 2
            for device in pending:
                print(
                    f"{device.device_id}\t{device.name}\t"
                    f"requested: {_format_time(device.requested_at)}"
                )
            print("Approve one with: deskwave-host pair <six-digit-code>")
            return 0
        approved = store.approve_pairing(code)
    except PairingError as error:
        print(f"Pairing failed: {error}", file=sys.stderr)
        return 2
    finally:
        store.close()
    print(f"Approved {approved.name} ({approved.device_id}).")
    print("The approval expires in five minutes if the device does not collect it.")
    return 0


def _revoke(config: HostConfig, device_id: str) -> int:
    store = _store(config)
    try:
        revoked = store.revoke(device_id)
    finally:
        store.close()
    if not revoked:
        print(f"No paired device named {device_id}.", file=sys.stderr)
        return 2
    print(f"Revoked {device_id}.")
    return 0


@dataclass(slots=True)
class DoctorResult:
    level: str
    name: str
    detail: str


async def _probe_dbus() -> DoctorResult:
    try:
        bus = await asyncio.wait_for(MessageBus(bus_type=BusType.SESSION).connect(), timeout=2.0)
        introspection = await asyncio.wait_for(
            bus.introspect("org.freedesktop.DBus", "/org/freedesktop/DBus"), timeout=1.5
        )
        proxy = bus.get_proxy_object("org.freedesktop.DBus", "/org/freedesktop/DBus", introspection)
        interface: Any = proxy.get_interface("org.freedesktop.DBus")
        names = await asyncio.wait_for(interface.call_list_names(), timeout=1.5)
        players = [str(name) for name in names if str(name).startswith("org.mpris.MediaPlayer2.")]
        bus.disconnect()  # type: ignore[no-untyped-call]
    except Exception as error:  # diagnostic boundary: report the library's exact failure
        return DoctorResult("FAIL", "D-Bus", str(error))
    if players:
        return DoctorResult("PASS", "MPRIS", f"detected {len(players)} player(s)")
    return DoctorResult("WARN", "MPRIS", "D-Bus works, but no MPRIS players are running")


async def _doctor(config: HostConfig) -> int:
    results: list[DoctorResult] = []
    try:
        config.paths.ensure()
        results.append(DoctorResult("PASS", "Configuration", str(config.paths.config_dir)))
    except OSError as error:
        results.append(DoctorResult("FAIL", "Configuration", str(error)))
    health = await _fetch_health(config)
    if health is not None:
        results.append(DoctorResult("PASS", "Service", f"listening on port {config.port}"))
    else:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind((config.bind, config.port))
            results.append(DoctorResult("PASS", "Network binding", f"{config.bind}:{config.port}"))
        except OSError as error:
            results.append(DoctorResult("FAIL", "Network binding", str(error)))
        finally:
            sock.close()
    address = discover_lan_address()
    if address == "127.0.0.1":
        results.append(DoctorResult("WARN", "mDNS", "no non-loopback IPv4 route detected"))
    else:
        results.append(DoctorResult("PASS", "mDNS", f"will advertise {address}"))
    results.append(await _probe_dbus())
    for result in results:
        print(f"[{result.level}] {result.name}: {result.detail}")
    return 1 if any(result.level == "FAIL" for result in results) else 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="deskwave-host")
    parser.add_argument("--version", action="version", version=f"DeskWave Host {__version__}")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("run", help="run the DeskWave Host daemon")
    subparsers.add_parser("status", help="query the running host")
    subparsers.add_parser("devices", help="list paired devices")
    pair = subparsers.add_parser("pair", help="list or approve pending device pairing")
    pair.add_argument("code", nargs="?", help="six-digit code displayed by DeskWave")
    revoke = subparsers.add_parser("revoke", help="revoke a paired device token")
    revoke.add_argument("device_id")
    subparsers.add_parser("doctor", help="check configuration, D-Bus, MPRIS, and networking")
    subparsers.add_parser("version", help="print the host version")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = build_parser().parse_args(argv)
    if arguments.command == "version":
        print(f"DeskWave Host {__version__} (protocol 1)")
        return 0
    try:
        config = _config()
    except ConfigError as error:
        print(f"Configuration error: {error}", file=sys.stderr)
        return 2
    if arguments.command == "run":
        return asyncio.run(_run_daemon(config))
    if arguments.command == "status":
        return asyncio.run(_status(config))
    if arguments.command == "devices":
        return _devices(config)
    if arguments.command == "pair":
        return _pair(config, arguments.code)
    if arguments.command == "revoke":
        return _revoke(config, arguments.device_id)
    if arguments.command == "doctor":
        return asyncio.run(_doctor(config))
    return 2
