#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
host_directory="$(cd -- "${script_dir}/.." && pwd)"
python_command="${PYTHON:-python3}"
venv_directory="${HOME}/.local/share/deskwave/venv"
bin_directory="${HOME}/.local/bin"
command_link="${bin_directory}/deskwave-host"
config_directory="${HOME}/.config/deskwave"
cache_directory="${HOME}/.cache/deskwave"
state_directory="${HOME}/.local/state/deskwave"

if [[ $# -gt 1 || ( $# -eq 1 && "${1:-}" != "--enable" ) ]]; then
    echo "Usage: $0 [--enable]" >&2
    exit 2
fi

if ! command -v "${python_command}" >/dev/null 2>&1; then
    echo "Python 3.11 or newer is required." >&2
    exit 2
fi

python_version="$("${python_command}" -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
if ! "${python_command}" -c 'import sys; raise SystemExit(sys.version_info < (3, 11))'; then
    echo "Python 3.11 or newer is required; found ${python_version}." >&2
    exit 2
fi

if [[ -e "${command_link}" || -L "${command_link}" ]]; then
    if [[ ! -L "${command_link}" || "$(readlink -- "${command_link}")" != "${venv_directory}/bin/deskwave-host" ]]; then
        echo "Refusing to replace existing ${command_link}; move it and run this installer again." >&2
        exit 3
    fi
fi

install -d -m 0755 "$(dirname -- "${venv_directory}")" "${bin_directory}"
install -d -m 0700 "${config_directory}" "${cache_directory}" "${state_directory}"
"${python_command}" -m venv "${venv_directory}"
"${venv_directory}/bin/python" -m pip install --disable-pip-version-check --upgrade "${host_directory}"

if [[ ! -L "${command_link}" ]]; then
    ln -s "${venv_directory}/bin/deskwave-host" "${command_link}"
fi

if [[ ! -e "${config_directory}/config.toml" ]]; then
    install -m 0600 "${host_directory}/config.example.toml" "${config_directory}/config.toml"
fi

"${script_dir}/install-user-service.sh" "${1:-}"

echo "DeskWave Host installed in ${venv_directory}"
echo "CLI: ${command_link}"
if [[ ":${PATH}:" != *":${bin_directory}:"* ]]; then
    echo "Add ${bin_directory} to PATH to run deskwave-host without its full path."
fi
