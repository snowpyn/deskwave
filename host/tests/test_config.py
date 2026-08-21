from __future__ import annotations

from pathlib import Path

import pytest

from deskwave_host.config import ConfigError, HostConfig, HostPaths


def make_paths(tmp_path: Path) -> HostPaths:
    result = HostPaths(tmp_path / "config", tmp_path / "cache", tmp_path / "state")
    result.ensure()
    return result


def test_loads_toml_and_environment_override(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    host_paths = make_paths(tmp_path)
    (host_paths.config_dir / "config.toml").write_text(
        '[host]\nport = 9000\nlog_level = "DEBUG"\n', encoding="utf-8"
    )
    monkeypatch.setenv("DESKWAVE_PORT", "9001")
    config = HostConfig.load(host_paths)
    assert config.port == 9001
    assert config.log_level == "DEBUG"


def test_unknown_configuration_is_rejected(tmp_path: Path) -> None:
    host_paths = make_paths(tmp_path)
    (host_paths.config_dir / "config.toml").write_text(
        "[host]\nsecret_backdoor = true\n", encoding="utf-8"
    )
    with pytest.raises(ConfigError, match="unknown"):
        HostConfig.load(host_paths)
