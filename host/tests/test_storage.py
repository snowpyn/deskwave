from __future__ import annotations

from pathlib import Path

import pytest

from deskwave_host.storage import DeviceStore, PairingError


class Clock:
    def __init__(self) -> None:
        self.now = 1_700_000_000.0

    def __call__(self) -> float:
        return self.now


def test_pairing_round_trip_and_authentication(tmp_path: Path) -> None:
    clock = Clock()
    store = DeviceStore(tmp_path / "devices.sqlite3", clock=clock)
    store.request_pairing("dw-aabbccdd", "DeskWave", "123456")
    assert [pending.device_id for pending in store.list_pending()] == ["dw-aabbccdd"]
    approved = store.approve_pairing("123456")
    assert approved.device_id == "dw-aabbccdd"
    token = store.consume_approval("dw-aabbccdd", "123456")
    assert token is not None
    assert store.authenticate(token) == "dw-aabbccdd"
    assert store.authenticate(f"{token}x") is None
    with pytest.raises(PairingError):
        store.consume_approval("dw-aabbccdd", "123456")
    store.close()


def test_pending_pairing_expires(tmp_path: Path) -> None:
    clock = Clock()
    store = DeviceStore(tmp_path / "devices.sqlite3", clock=clock)
    store.request_pairing("dw-aabbccdd", "DeskWave", "123456")
    clock.now += 301
    assert store.list_pending() == []
    with pytest.raises(PairingError):
        store.approve_pairing("123456")
    store.close()


@pytest.mark.parametrize(
    ("device_id", "name", "code"),
    [
        ("short", "DeskWave", "123456"),
        ("dw-aabbccdd", "", "123456"),
        ("dw-aabbccdd", "DeskWave", "\uff11\uff12\uff13\uff14\uff15\uff16"),
    ],
)
def test_invalid_pairing_fields_are_rejected(
    tmp_path: Path, device_id: str, name: str, code: str
) -> None:
    store = DeviceStore(tmp_path / "devices.sqlite3")
    with pytest.raises(PairingError):
        store.request_pairing(device_id, name, code)
    store.close()
