from deskwave_host import __version__
from deskwave_host.cli import main


def test_version_is_release_candidate() -> None:
    assert __version__ == "0.1.0"


def test_cli_accepts_no_arguments() -> None:
    assert main([]) == 0
