# Contributing to DeskWave

DeskWave favors correctness, recovery, hardware safety, and explicit failures
over feature breadth. Keep changes scoped and preserve the separation between
desktop integration, protocol, portable firmware logic, hardware adapters, and
UI rendering.

## Before changing code

- Read [Architecture](docs/ARCHITECTURE.md), [Protocol](docs/PROTOCOL.md), and
  [Wiring](docs/WIRING.md) for the boundary being changed.
- Do not change the ESP32-D0WD-V3 GPIO map without checking uniqueness,
  documentation, and physical-test impact.
- Treat protocol 1 as a compatibility boundary. Additive optional fields are
  acceptable; changed meaning/units/required fields need a new protocol version.
- Never add fake production metadata, silent command success, unbounded network
  operations, or a blocking input path.

## Development setup

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -e 'host[dev]'
.venv/bin/python -m pip install platformio==6.1.19 clang-format==18.1.8
```

Format changed C/C++ files with the repository `.clang-format` and Python with
Ruff. Do not format vendored PlatformIO dependencies.

```bash
find firmware -type f \( -name '*.cpp' -o -name '*.h' \) -print0 \
  | xargs -0 .venv/bin/clang-format -i
.venv/bin/ruff format host
.venv/bin/ruff check --fix host
```

Review automatic fixes before committing.

## Required checks

```bash
.venv/bin/python scripts/check_version.py
.venv/bin/pio run -e esp32-d0wd-v3
.venv/bin/pio test -e native
.venv/bin/ruff format --check host scripts
.venv/bin/ruff check host scripts
.venv/bin/mypy host/src
.venv/bin/pytest -q host/tests --ignore=host/tests/integration
.venv/bin/pytest -q host/tests/integration
```

Behavior changes require focused tests. Host protocol/API behavior belongs in
unit or aiohttp integration tests. Platform-independent input, state, backoff,
progress, pin, and mapping behavior belongs in the native firmware suite.
Hardware-dependent changes require an updated
[smoke-test procedure or completed record](docs/HARDWARE_SMOKE_TEST.md).

## Commit and review hygiene

- Use descriptive, focused commits such as `feat(ui): ...`, `fix(host): ...`,
  `test(protocol): ...`, or `docs: ...`.
- Do not rewrite shared history unnecessarily.
- Explain failure behavior, ownership/concurrency changes, security impact, and
  physical verification status in the pull request.
- Preserve unrelated work and generated/runtime files outside Git.
- Fix a failing gate; never disable it, weaken assertions, or claim an unexecuted
  test passed.

## Secrets and private data

Never stage Wi-Fi credentials, pairing codes/tokens, `.env`, local config,
SQLite state, artwork cache, firmware dumps from a paired device, private URLs,
or unsanitized logs. Follow [SECURITY.md](SECURITY.md) for vulnerability reports.

By contributing, you agree that your contribution is licensed under the
repository's [MIT License](LICENSE).
