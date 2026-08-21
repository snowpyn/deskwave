from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from aiohttp import ClientWebSocketResponse, WSMsgType
from aiohttp.test_utils import TestClient, TestServer
from conftest import FakeBackend

from deskwave_host.api import create_app
from deskwave_host.artwork import ArtworkCache
from deskwave_host.config import HostConfig
from deskwave_host.models import PlaybackState, PlaybackStatus
from deskwave_host.protocol import make_message
from deskwave_host.service import MediaService
from deskwave_host.storage import DeviceStore


async def pair(client: TestClient, store: DeviceStore) -> str:
    response = await client.post(
        "/v1/pairing/request",
        json={"device_id": "dw-aabbccdd", "name": "DeskWave", "code": "123456"},
    )
    assert response.status == 202
    store.approve_pairing("123456")
    response = await client.post(
        "/v1/pairing/status", json={"device_id": "dw-aabbccdd", "code": "123456"}
    )
    assert response.status == 200
    document = await response.json()
    return str(document["token"])


async def receive_type(websocket: ClientWebSocketResponse, expected: str) -> dict[str, Any]:
    for _ in range(5):
        incoming = await websocket.receive_json()
        if incoming["type"] == expected:
            return incoming
    raise AssertionError(f"did not receive {expected}")


async def test_pair_control_disconnect_and_reconnect(
    host_config: HostConfig, tmp_path: Path
) -> None:
    initial = PlaybackState(
        title="A real track",
        artists=("Artist",),
        status=PlaybackStatus.PLAYING,
        player_id="org.mpris.MediaPlayer2.test",
        player_name="Test Player",
        can_control=True,
    )
    backend = FakeBackend(initial)
    store = DeviceStore(tmp_path / "devices.sqlite3")
    artwork = ArtworkCache(host_config)
    service = MediaService(backend, artwork)
    app = create_app(host_config, store, service, artwork)
    async with TestClient(TestServer(app)) as client:
        unauthorized = await client.get("/v1/players")
        assert unauthorized.status == 401
        token = await pair(client, store)
        headers = {"Authorization": f"Bearer {token}"}
        websocket = await client.ws_connect("/v1/ws", headers=headers)
        hello = await receive_type(websocket, "hello")
        assert hello["payload"]["protocol"] == 1
        state = await receive_type(websocket, "playback_state")
        assert state["payload"]["title"] == "A real track"

        await websocket.send_str(json.dumps(make_message("list_players", 43, {})))
        player_list = await receive_type(websocket, "players")
        assert player_list["payload"]["players"][0]["name"] == "Test Player"

        command = make_message("control", 44, {"command": "toggle"})
        await websocket.send_str(json.dumps(command))
        result = await receive_type(websocket, "command_result")
        assert result["payload"]["success"] is True
        await websocket.send_str(json.dumps(command))
        duplicate = await receive_type(websocket, "command_result")
        assert duplicate["payload"]["request_sequence"] == 44
        assert len(backend.commands) == 1
        await websocket.close()

        reconnected = await client.ws_connect("/v1/ws", headers=headers)
        await receive_type(reconnected, "hello")
        recovered = await receive_type(reconnected, "playback_state")
        assert recovered["payload"]["status"] == "paused"
        for _ in range(3):
            await reconnected.send_bytes(b"unsupported")
            invalid = await receive_type(reconnected, "error")
            assert invalid["payload"]["code"] == "unsupported_frame"
        closing = await reconnected.receive()
        assert closing.type in {WSMsgType.CLOSE, WSMsgType.CLOSED}
        await reconnected.close()
    store.close()
