from __future__ import annotations

import pytest

from deskwave_host.cli import main


def test_version_command(capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["version"]) == 0
    assert "DeskWave Host 0.1.0" in capsys.readouterr().out
