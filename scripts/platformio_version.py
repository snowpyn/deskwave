import re
from pathlib import Path

Import("env")  # noqa: F821 - provided by the PlatformIO/SCons script runtime
pio_env = env  # noqa: F821 - imported into this script by the line above


project_dir = Path(pio_env["PROJECT_DIR"])
version = (project_dir / "VERSION").read_text(encoding="ascii").strip()
if not re.fullmatch(r"\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?", version):
    raise RuntimeError("VERSION must contain a valid semantic version")

pio_env.Append(CPPDEFINES=[("DESKWAVE_VERSION", f'\\"{version}\\"')])
