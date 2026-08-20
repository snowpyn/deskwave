"""mDNS registration for automatic DeskWave Host discovery."""

from __future__ import annotations

import asyncio
import logging
import socket
from dataclasses import dataclass

from zeroconf import IPVersion, ServiceInfo, Zeroconf

LOGGER = logging.getLogger("discovery")
SERVICE_TYPE = "_deskwave._tcp.local."


def discover_lan_address() -> str:
    """Ask the route table for a LAN source address without sending payload data."""

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("192.0.2.1", 9))
        address = str(sock.getsockname()[0])
    except OSError:
        address = "127.0.0.1"
    finally:
        sock.close()
    return address


@dataclass(slots=True)
class DiscoveryService:
    service_name: str
    port: int
    _zeroconf: Zeroconf | None = None
    _info: ServiceInfo | None = None

    async def start(self) -> None:
        address = discover_lan_address()
        if address == "127.0.0.1":
            LOGGER.warning("No LAN address is available; mDNS registration is local-only")
        hostname = socket.gethostname().split(".", 1)[0] or "deskwave-host"
        instance = f"{self.service_name} on {hostname}.{SERVICE_TYPE}"
        self._info = ServiceInfo(
            SERVICE_TYPE,
            instance,
            addresses=[socket.inet_aton(address)],
            port=self.port,
            properties={"protocol": "1", "path": "/v1/ws"},
            server=f"{hostname}.local.",
        )
        self._zeroconf = Zeroconf(ip_version=IPVersion.V4Only)
        await asyncio.to_thread(self._zeroconf.register_service, self._info)
        LOGGER.info("Published %s at %s:%d", instance, address, self.port)

    async def stop(self) -> None:
        if self._zeroconf is None:
            return
        if self._info is not None:
            await asyncio.to_thread(self._zeroconf.unregister_service, self._info)
        await asyncio.to_thread(self._zeroconf.close)
        self._zeroconf = None
        self._info = None
