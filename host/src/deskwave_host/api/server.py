"""Authenticated local HTTP/WebSocket server for DeskWave devices."""

from __future__ import annotations

import asyncio
import json
import logging
from collections import OrderedDict, deque
from collections.abc import Awaitable, Callable
from dataclasses import dataclass, field
from time import monotonic
from typing import Any

from aiohttp import WSCloseCode, WSMsgType, web

from deskwave_host import __version__
from deskwave_host.artwork import ArtworkCache
from deskwave_host.config import HostConfig
from deskwave_host.discovery import DiscoveryService
from deskwave_host.models import PlaybackState, PlayerSummary
from deskwave_host.protocol import (
    MAX_MESSAGE_BYTES,
    MAX_SEQUENCE,
    PROTOCOL_VERSION,
    IncomingMessage,
    ProtocolError,
    make_message,
    parse_message,
)
from deskwave_host.service import MediaService
from deskwave_host.storage import DeviceStore, PairingError

LOGGER = logging.getLogger("network")
CLOCK_SYNC_SECONDS = 30
CONFIG_KEY: web.AppKey[HostConfig] = web.AppKey("config", HostConfig)
STORE_KEY: web.AppKey[DeviceStore] = web.AppKey("store", DeviceStore)
SERVICE_KEY: web.AppKey[MediaService] = web.AppKey("service", MediaService)
ARTWORK_KEY: web.AppKey[ArtworkCache] = web.AppKey("artwork", ArtworkCache)
RATE_LIMIT_KEY: web.AppKey[RateLimiter]  # assigned after class definition
MAX_PAIRING_BODY = 2048
MAX_DEVICE_PLAYERS = 6
MAX_DEVICE_MESSAGE_BYTES = 8_192
MAX_DEVICE_TEXT_BYTES = 128
MAX_DEVICE_PLAYER_NAME_BYTES = 64
MAX_DEVICE_FEEDBACK_BYTES = 96
DEVICE_CONTROL_TRANSLATION = str.maketrans(
    {codepoint: " " for codepoint in (*range(0x20), *range(0x7F, 0xA0))}
)


@dataclass(slots=True)
class RateLimiter:
    limit: int = 12
    window_seconds: float = 60.0
    maximum_identities: int = 256
    _attempts: OrderedDict[str, deque[float]] = field(default_factory=OrderedDict)

    def allow(self, identity: str) -> bool:
        now = monotonic()
        cutoff = now - self.window_seconds
        attempts = self._attempts.get(identity)
        if attempts is None:
            for stale_identity, stale_attempts in tuple(self._attempts.items()):
                while stale_attempts and stale_attempts[0] < cutoff:
                    stale_attempts.popleft()
                if not stale_attempts:
                    self._attempts.pop(stale_identity, None)
            while len(self._attempts) >= self.maximum_identities:
                self._attempts.popitem(last=False)
            attempts = deque()
            self._attempts[identity] = attempts
        else:
            self._attempts.move_to_end(identity)
        while attempts and attempts[0] < cutoff:
            attempts.popleft()
        if len(attempts) >= self.limit:
            return False
        attempts.append(now)
        return True


RATE_LIMIT_KEY = web.AppKey("rate_limiter", RateLimiter)


def _remote_identity(request: web.Request) -> str:
    peer = request.transport.get_extra_info("peername") if request.transport else None
    return str(peer[0]) if isinstance(peer, tuple) and peer else "unknown"


def _bearer_token(request: web.Request) -> str | None:
    value = request.headers.get("Authorization", "")
    if not value.startswith("Bearer "):
        return None
    token = value[7:]
    return token if token and " " not in token else None


def _authenticate(request: web.Request) -> str | None:
    token = _bearer_token(request)
    return request.app[STORE_KEY].authenticate(token) if token else None


def _bounded_utf8(value: str, maximum_bytes: int) -> str:
    """Return valid UTF-8 that fits the device's fixed-size text buffer."""

    encoded = value.translate(DEVICE_CONTROL_TRANSLATION).encode("utf-8", errors="replace")
    if len(encoded) <= maximum_bytes:
        return encoded.decode("utf-8")
    return encoded[:maximum_bytes].decode("utf-8", errors="ignore")


def _device_artists(artists: tuple[str, ...]) -> list[str]:
    """Bound the first three artists to the firmware's joined 128-byte field."""

    result: list[str] = []
    used = 0
    for artist in artists[:3]:
        separator_bytes = 2 if result else 0
        available = MAX_DEVICE_TEXT_BYTES - used - separator_bytes
        if available <= 0:
            break
        bounded = _bounded_utf8(artist, available)
        if not bounded:
            continue
        result.append(bounded)
        used += separator_bytes + len(bounded.encode("utf-8"))
    return result


def _encode_device_message(message: dict[str, Any]) -> str:
    """Serialize one bounded firmware frame with deterministic UTF-8 JSON."""

    encoded = json.dumps(
        message,
        ensure_ascii=False,
        allow_nan=False,
        separators=(",", ":"),
    )
    if len(encoded.encode("utf-8")) > MAX_DEVICE_MESSAGE_BYTES:
        raise ValueError("outbound device message exceeds the firmware receive limit")
    return encoded


def _state_payload(state: PlaybackState) -> dict[str, Any]:
    payload = state.to_payload()
    payload["title"] = _bounded_utf8(state.title, MAX_DEVICE_TEXT_BYTES)
    payload["artists"] = _device_artists(state.artists)
    payload["album"] = _bounded_utf8(state.album, MAX_DEVICE_TEXT_BYTES)
    payload["player_name"] = (
        None
        if state.player_name is None
        else _bounded_utf8(state.player_name, MAX_DEVICE_PLAYER_NAME_BYTES)
    )
    payload["player_id"] = (
        None if state.player_id is None else _bounded_utf8(state.player_id, MAX_DEVICE_TEXT_BYTES)
    )
    payload["track_id"] = (
        None if state.track_id is None else _bounded_utf8(state.track_id, MAX_DEVICE_TEXT_BYTES)
    )
    payload.pop("queue", None)
    capabilities = payload.get("capabilities")
    if isinstance(capabilities, dict):
        capabilities.pop("queue", None)
    captions = {
        "status": state.lyrics_status.value,
        "lines": [
            {
                "time_ms": line.time_ms,
                "text": _bounded_utf8(line.text, MAX_DEVICE_TEXT_BYTES),
            }
            for line in state.lyrics
        ],
    }
    if state.media_kind.value == "podcast":
        payload.pop("lyrics", None)
        payload["transcript"] = captions
    else:
        payload["lyrics"] = captions
    payload["artwork_path"] = (
        f"/v1/artwork/{state.artwork_id}.jpg" if state.artwork_id is not None else None
    )
    return payload


def _players_payload(summaries: list[PlayerSummary]) -> list[dict[str, str]]:
    return [
        {
            "id": _bounded_utf8(player.player_id, MAX_DEVICE_TEXT_BYTES),
            "name": _bounded_utf8(player.name, MAX_DEVICE_PLAYER_NAME_BYTES),
            "status": player.status.value,
        }
        for player in summaries[:MAX_DEVICE_PLAYERS]
    ]


async def health(request: web.Request) -> web.Response:
    state = request.app[SERVICE_KEY].state
    return web.json_response(
        {
            "status": "ok",
            "version": __version__,
            "protocol": PROTOCOL_VERSION,
            "player_available": state.player_id is not None,
            "active_player": state.player_name,
        },
        headers={"Cache-Control": "no-store"},
    )


async def pairing_request(request: web.Request) -> web.Response:
    limiter = request.app[RATE_LIMIT_KEY]
    if not limiter.allow(f"pair-request:{_remote_identity(request)}"):
        raise web.HTTPTooManyRequests(text="pairing request rate limit exceeded")
    if request.content_length is not None and request.content_length > MAX_PAIRING_BODY:
        raise web.HTTPRequestEntityTooLarge(
            max_size=MAX_PAIRING_BODY, actual_size=request.content_length
        )
    try:
        document = await request.json(loads=json.loads)
    except (json.JSONDecodeError, UnicodeDecodeError):
        raise web.HTTPBadRequest(text="request body must be JSON") from None
    if not isinstance(document, dict):
        raise web.HTTPBadRequest(text="request body must be an object")
    device_id = document.get("device_id")
    name = document.get("name")
    code = document.get("code")
    if not isinstance(device_id, str) or not isinstance(name, str) or not isinstance(code, str):
        raise web.HTTPBadRequest(text="device_id, name, and code must be strings")
    try:
        request.app[STORE_KEY].request_pairing(device_id, name, code)
    except PairingError as error:
        raise web.HTTPBadRequest(text=str(error)) from error
    LOGGER.info("Pairing requested by device %s", device_id)
    return web.json_response(
        {"status": "pending", "expires_in_seconds": 300},
        status=202,
        headers={"Cache-Control": "no-store"},
    )


async def pairing_status(request: web.Request) -> web.Response:
    limiter = request.app[RATE_LIMIT_KEY]
    if not limiter.allow(f"pair-status:{_remote_identity(request)}"):
        raise web.HTTPTooManyRequests(text="pairing status rate limit exceeded")
    try:
        document = await request.json(loads=json.loads)
    except (json.JSONDecodeError, UnicodeDecodeError):
        raise web.HTTPBadRequest(text="request body must be JSON") from None
    if not isinstance(document, dict):
        raise web.HTTPBadRequest(text="request body must be an object")
    device_id = document.get("device_id")
    code = document.get("code")
    if not isinstance(device_id, str) or not isinstance(code, str):
        raise web.HTTPBadRequest(text="device_id and code must be strings")
    try:
        token = request.app[STORE_KEY].consume_approval(device_id, code)
    except PairingError as error:
        raise web.HTTPNotFound(text=str(error)) from error
    if token is None:
        return web.json_response(
            {"status": "pending"}, status=202, headers={"Cache-Control": "no-store"}
        )
    LOGGER.info("Pairing completed for device %s", device_id)
    return web.json_response(
        {"status": "paired", "token": token}, headers={"Cache-Control": "no-store"}
    )


async def artwork(request: web.Request) -> web.StreamResponse:
    device_id = _authenticate(request)
    if device_id is None:
        raise web.HTTPUnauthorized(headers={"WWW-Authenticate": "Bearer"})
    artwork_id = request.match_info["artwork_id"]
    path = request.app[ARTWORK_KEY].path_for(artwork_id)
    if path is None:
        raise web.HTTPNotFound(text="artwork is not cached")
    request.app[STORE_KEY].touch(device_id)
    # Do not use FileResponse/sendfile here. ESP32 HTTPClient clients on this
    # LAN can receive the response headers but fail to consume the zero-copy
    # body, leaving the device with a 326-byte-looking response and an
    # incomplete artwork cache. Normal buffered writes keep the body bounded
    # by the host's normalized-artwork limit and make Content-Length explicit.
    body = await asyncio.to_thread(path.read_bytes)
    response = web.Response(body=body)
    response.headers.update(
        {
            "Cache-Control": "private, max-age=31536000, immutable",
            "ETag": f'"{artwork_id}"',
            "Content-Type": "image/jpeg",
            "X-Content-Type-Options": "nosniff",
        }
    )
    return response


async def players(request: web.Request) -> web.Response:
    device_id = _authenticate(request)
    if device_id is None:
        raise web.HTTPUnauthorized(headers={"WWW-Authenticate": "Bearer"})
    request.app[STORE_KEY].touch(device_id)
    summaries = await request.app[SERVICE_KEY].players()
    return web.json_response(
        {"players": _players_payload(summaries)},
        headers={"Cache-Control": "no-store"},
    )


class DeviceSession:
    def __init__(
        self, request: web.Request, websocket: web.WebSocketResponse, device_id: str
    ) -> None:
        self.request = request
        self.websocket = websocket
        self.device_id = device_id
        self.service = request.app[SERVICE_KEY]
        self.store = request.app[STORE_KEY]
        self.sequence = 0
        self.invalid_messages = 0
        self.results: OrderedDict[int, dict[str, Any]] = OrderedDict()

    def _next_sequence(self) -> int:
        self.sequence = (self.sequence + 1) % (MAX_SEQUENCE + 1)
        return self.sequence

    async def send(self, message_type: str, payload: dict[str, Any]) -> dict[str, Any]:
        message = make_message(message_type, self._next_sequence(), payload)
        await self.websocket.send_str(_encode_device_message(message))
        return message

    async def send_initial(self) -> None:
        await self.send(
            "hello",
            {
                "host_version": __version__,
                "protocol": PROTOCOL_VERSION,
                "device_id": self.device_id,
                "heartbeat_seconds": 20,
            },
        )
        await self.send("playback_state", _state_payload(self.service.state))

    async def send_updates(self, queue: asyncio.Queue[PlaybackState]) -> None:
        while not self.websocket.closed:
            try:
                async with asyncio.timeout(CLOCK_SYNC_SECONDS):
                    state = await queue.get()
            except TimeoutError:
                await self.send("clock_sync", {})
            else:
                await self.send("playback_state", _state_payload(state))

    async def handle(self, message: IncomingMessage) -> None:
        self.store.touch(self.device_id)
        if message.message_type == "ping":
            nonce = message.payload.get("nonce")
            if isinstance(nonce, str):
                nonce = _bounded_utf8(nonce, MAX_DEVICE_TEXT_BYTES)
            elif isinstance(nonce, int) and not -(2**63) <= nonce <= 2**63 - 1:
                nonce = None
            await self.send(
                "pong",
                {
                    "request_sequence": message.sequence,
                    "nonce": nonce if isinstance(nonce, (str, int)) else None,
                },
            )
            return
        if message.message_type == "device_status":
            return
        if message.message_type == "list_players":
            summaries = await self.service.players()
            await self.send(
                "players",
                {
                    "request_sequence": message.sequence,
                    "players": _players_payload(summaries),
                },
            )
            return
        cached = self.results.get(message.sequence)
        if cached is not None:
            await self.websocket.send_str(_encode_device_message(cached))
            return
        command = str(message.payload["command"])
        arguments = {key: value for key, value in message.payload.items() if key != "command"}
        result = await self.service.command(command, arguments)
        response = await self.send(
            "command_result",
            {
                "request_sequence": message.sequence,
                "command": command,
                "success": result.success,
                "error": (
                    None
                    if result.error is None
                    else _bounded_utf8(result.error, MAX_DEVICE_FEEDBACK_BYTES)
                ),
            },
        )
        self.results[message.sequence] = response
        while len(self.results) > 64:
            self.results.popitem(last=False)


async def websocket(request: web.Request) -> web.StreamResponse:
    device_id = _authenticate(request)
    if device_id is None:
        raise web.HTTPUnauthorized(headers={"WWW-Authenticate": "Bearer"})
    ws = web.WebSocketResponse(
        heartbeat=20.0,
        receive_timeout=60.0,
        max_msg_size=MAX_MESSAGE_BYTES,
        compress=False,
    )
    await ws.prepare(request)
    session = DeviceSession(request, ws, device_id)
    queue = request.app[SERVICE_KEY].subscribe()
    sender = asyncio.create_task(session.send_updates(queue), name=f"ws-send-{device_id}")
    request.app[STORE_KEY].touch(device_id)
    LOGGER.info("Device %s connected", device_id)
    try:
        await session.send_initial()
        async for incoming in ws:
            if incoming.type is WSMsgType.TEXT:
                try:
                    message = parse_message(incoming.data)
                    await session.handle(message)
                    session.invalid_messages = 0
                except ProtocolError as error:
                    session.invalid_messages += 1
                    await session.send(
                        "error",
                        {"code": "invalid_message", "message": str(error)},
                    )
                    if session.invalid_messages >= 3:
                        await ws.close(
                            code=WSCloseCode.POLICY_VIOLATION,
                            message=b"too many invalid messages",
                        )
            elif incoming.type is WSMsgType.BINARY:
                session.invalid_messages += 1
                await session.send(
                    "error",
                    {"code": "unsupported_frame", "message": "binary frames are not supported"},
                )
                if session.invalid_messages >= 3:
                    await ws.close(
                        code=WSCloseCode.POLICY_VIOLATION,
                        message=b"too many invalid messages",
                    )
            elif incoming.type in {WSMsgType.ERROR, WSMsgType.CLOSE, WSMsgType.CLOSING}:
                break
    except TimeoutError:
        LOGGER.info("Device %s timed out", device_id)
    finally:
        request.app[SERVICE_KEY].unsubscribe(queue)
        sender.cancel()
        try:
            await sender
        except asyncio.CancelledError:
            pass
        LOGGER.info("Device %s disconnected", device_id)
    return ws


def create_app(
    config: HostConfig,
    store: DeviceStore,
    service: MediaService,
    artwork_cache: ArtworkCache,
) -> web.Application:
    app = web.Application(client_max_size=MAX_PAIRING_BODY)
    app[CONFIG_KEY] = config
    app[STORE_KEY] = store
    app[SERVICE_KEY] = service
    app[ARTWORK_KEY] = artwork_cache
    app[RATE_LIMIT_KEY] = RateLimiter()
    app.add_routes(
        [
            web.get("/healthz", health),
            web.post("/v1/pairing/request", pairing_request),
            web.post("/v1/pairing/status", pairing_status),
            web.get("/v1/artwork/{artwork_id}.jpg", artwork),
            web.get("/v1/players", players),
            web.get("/v1/ws", websocket),
        ]
    )

    async def start_service(_: web.Application) -> None:
        await service.start()

    async def stop_service(_: web.Application) -> None:
        await service.stop()

    app.on_startup.append(start_service)
    app.on_cleanup.append(stop_service)
    return app


async def serve(
    config: HostConfig,
    store: DeviceStore,
    service: MediaService,
    artwork_cache: ArtworkCache,
    stop_event: asyncio.Event,
    site_started: Callable[[], Awaitable[None]] | None = None,
) -> None:
    app = create_app(config, store, service, artwork_cache)
    runner = web.AppRunner(app, access_log=LOGGER)
    discovery = DiscoveryService(config.service_name, config.port)
    await runner.setup()
    site = web.TCPSite(runner, config.bind, config.port, shutdown_timeout=5.0)
    try:
        await site.start()
        LOGGER.info("DeskWave Host listening on %s:%d", config.bind, config.port)
        try:
            await discovery.start()
        except (OSError, RuntimeError) as error:
            LOGGER.warning("mDNS discovery could not start: %s", error)
        if site_started is not None:
            await site_started()
        await stop_event.wait()
    finally:
        await discovery.stop()
        await runner.cleanup()
