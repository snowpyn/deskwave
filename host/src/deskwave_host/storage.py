"""SQLite-backed pairing and device storage."""

from __future__ import annotations

import hashlib
import hmac
import secrets
import sqlite3
import threading
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

PAIRING_TTL_SECONDS = 300
APPROVAL_TTL_SECONDS = 300


class PairingError(ValueError):
    """Pairing request or approval is invalid."""


@dataclass(frozen=True, slots=True)
class PendingDevice:
    device_id: str
    name: str
    requested_at: int


@dataclass(frozen=True, slots=True)
class PairedDevice:
    device_id: str
    name: str
    paired_at: int
    last_seen: int | None


def _token_hash(token: str) -> str:
    return hashlib.sha256(token.encode("utf-8")).hexdigest()


def _code_hash(device_id: str, code: str) -> str:
    return hashlib.sha256(f"{device_id}\0{code}".encode()).hexdigest()


class DeviceStore:
    """Small cross-process database used by both the daemon and its CLI."""

    def __init__(self, path: Path, clock: Callable[[], float] = time.time) -> None:
        self._path = path
        self._clock = clock
        self._lock = threading.RLock()
        path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        path.parent.chmod(0o700)
        self._connection = sqlite3.connect(path, timeout=5, check_same_thread=False)
        path.chmod(0o600)
        self._connection.row_factory = sqlite3.Row
        self._connection.execute("PRAGMA journal_mode=WAL")
        self._connection.execute("PRAGMA foreign_keys=ON")
        self._migrate()

    def _migrate(self) -> None:
        with self._lock, self._connection:
            self._connection.executescript(
                """
                CREATE TABLE IF NOT EXISTS schema_version (
                    version INTEGER NOT NULL
                );
                INSERT INTO schema_version(version)
                    SELECT 1 WHERE NOT EXISTS (SELECT 1 FROM schema_version);
                CREATE TABLE IF NOT EXISTS devices (
                    device_id TEXT PRIMARY KEY,
                    name TEXT NOT NULL,
                    token_hash TEXT NOT NULL UNIQUE,
                    paired_at INTEGER NOT NULL,
                    last_seen INTEGER
                );
                CREATE TABLE IF NOT EXISTS pending_pairings (
                    device_id TEXT PRIMARY KEY,
                    name TEXT NOT NULL,
                    code_hash TEXT NOT NULL,
                    requested_at INTEGER NOT NULL,
                    approved_token TEXT,
                    approved_at INTEGER
                );
                """
            )

    def close(self) -> None:
        with self._lock:
            self._connection.close()

    def request_pairing(self, device_id: str, name: str, code: str) -> None:
        if not 8 <= len(device_id) <= 64 or not all(
            character.isalnum() or character in "-_" for character in device_id
        ):
            raise PairingError("device_id is invalid")
        if not name or len(name) > 64 or any(ord(character) < 32 for character in name):
            raise PairingError("device name is invalid")
        if len(code) != 6 or not code.isascii() or not code.isdigit():
            raise PairingError("pairing code must be six ASCII digits")
        now = int(self._clock())
        hashed = _code_hash(device_id, code)
        with self._lock, self._connection:
            self._purge_locked(now)
            existing = self._connection.execute(
                "SELECT code_hash, requested_at FROM pending_pairings WHERE device_id = ?",
                (device_id,),
            ).fetchone()
            if existing and hmac.compare_digest(existing["code_hash"], hashed):
                self._connection.execute(
                    "UPDATE pending_pairings SET name = ? WHERE device_id = ?",
                    (name, device_id),
                )
                return
            self._connection.execute(
                """
                INSERT INTO pending_pairings(
                    device_id, name, code_hash, requested_at, approved_token, approved_at
                ) VALUES (?, ?, ?, ?, NULL, NULL)
                ON CONFLICT(device_id) DO UPDATE SET
                    name = excluded.name,
                    code_hash = excluded.code_hash,
                    requested_at = excluded.requested_at,
                    approved_token = NULL,
                    approved_at = NULL
                """,
                (device_id, name, hashed, now),
            )

    def list_pending(self) -> list[PendingDevice]:
        now = int(self._clock())
        with self._lock, self._connection:
            self._purge_locked(now)
            rows = self._connection.execute(
                """
                SELECT device_id, name, requested_at FROM pending_pairings
                WHERE approved_token IS NULL ORDER BY requested_at
                """
            ).fetchall()
        return [PendingDevice(row["device_id"], row["name"], row["requested_at"]) for row in rows]

    def approve_pairing(self, code: str) -> PairedDevice:
        if len(code) != 6 or not code.isascii() or not code.isdigit():
            raise PairingError("pairing code must be six ASCII digits")
        now = int(self._clock())
        with self._lock, self._connection:
            self._purge_locked(now)
            rows = self._connection.execute(
                """
                SELECT device_id, name, code_hash, requested_at FROM pending_pairings
                WHERE approved_token IS NULL
                """
            ).fetchall()
            matches = [
                row
                for row in rows
                if hmac.compare_digest(row["code_hash"], _code_hash(row["device_id"], code))
            ]
            if len(matches) != 1:
                raise PairingError("pairing code was not found or is ambiguous")
            row = matches[0]
            token = secrets.token_urlsafe(32)
            paired_at = now
            self._connection.execute(
                """
                INSERT INTO devices(device_id, name, token_hash, paired_at, last_seen)
                VALUES (?, ?, ?, ?, NULL)
                ON CONFLICT(device_id) DO UPDATE SET
                    name = excluded.name,
                    token_hash = excluded.token_hash,
                    paired_at = excluded.paired_at,
                    last_seen = NULL
                """,
                (row["device_id"], row["name"], _token_hash(token), paired_at),
            )
            self._connection.execute(
                """
                UPDATE pending_pairings SET approved_token = ?, approved_at = ?
                WHERE device_id = ?
                """,
                (token, now, row["device_id"]),
            )
        return PairedDevice(row["device_id"], row["name"], paired_at, None)

    def consume_approval(self, device_id: str, code: str) -> str | None:
        now = int(self._clock())
        with self._lock, self._connection:
            self._purge_locked(now)
            row = self._connection.execute(
                """
                SELECT code_hash, approved_token FROM pending_pairings WHERE device_id = ?
                """,
                (device_id,),
            ).fetchone()
            if row is None or not hmac.compare_digest(
                row["code_hash"], _code_hash(device_id, code)
            ):
                raise PairingError("pairing request was not found")
            token = row["approved_token"]
            if token is not None:
                self._connection.execute(
                    "DELETE FROM pending_pairings WHERE device_id = ?", (device_id,)
                )
                return str(token)
            return None

    def authenticate(self, token: str) -> str | None:
        if not 32 <= len(token) <= 128:
            return None
        candidate = _token_hash(token)
        with self._lock:
            rows = self._connection.execute("SELECT device_id, token_hash FROM devices").fetchall()
        for row in rows:
            if hmac.compare_digest(row["token_hash"], candidate):
                return str(row["device_id"])
        return None

    def touch(self, device_id: str) -> None:
        with self._lock, self._connection:
            self._connection.execute(
                "UPDATE devices SET last_seen = ? WHERE device_id = ?",
                (int(self._clock()), device_id),
            )

    def list_devices(self) -> list[PairedDevice]:
        with self._lock:
            rows = self._connection.execute(
                "SELECT device_id, name, paired_at, last_seen FROM devices ORDER BY name"
            ).fetchall()
        return [
            PairedDevice(row["device_id"], row["name"], row["paired_at"], row["last_seen"])
            for row in rows
        ]

    def revoke(self, device_id: str) -> bool:
        with self._lock, self._connection:
            cursor = self._connection.execute(
                "DELETE FROM devices WHERE device_id = ?", (device_id,)
            )
            return cursor.rowcount == 1

    def _purge_locked(self, now: int) -> None:
        self._connection.execute(
            """
            DELETE FROM pending_pairings
            WHERE (approved_at IS NULL AND requested_at < ?)
               OR (approved_at IS NOT NULL AND approved_at < ?)
            """,
            (now - PAIRING_TTL_SECONDS, now - APPROVAL_TTL_SECONDS),
        )
