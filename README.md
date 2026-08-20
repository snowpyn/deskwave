# DeskWave

DeskWave is an ESP32-S3 desktop music controller paired with a Linux host
service. The host integrates with MPRIS players and exposes authenticated media
state and controls to a dedicated network-connected display.

This repository is in active development toward the first `v0.1.0` release.
The current foundation establishes reproducible firmware and host layouts; the
feature documentation will be expanded alongside verified implementations.

## Repository layout

```text
firmware/  ESP32-S3 firmware
host/      Linux DeskWave Host package
docs/      Architecture and protocol documentation
```

## Foundation checks

```bash
python -m pip install platformio==6.1.19
pio run

python -m venv .venv
. .venv/bin/activate
python -m pip install -e 'host[test]'
pytest host/tests
```

DeskWave is licensed under the [MIT License](LICENSE).
