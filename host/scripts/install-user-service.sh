#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
unit_source="${script_dir}/../systemd/deskwave-host.service"
unit_directory="${XDG_CONFIG_HOME:-${HOME}/.config}/systemd/user"

install -d -m 0755 "${unit_directory}"
install -d -m 0700 "${HOME}/.config/deskwave"
install -d -m 0700 "${HOME}/.cache/deskwave"
install -d -m 0700 "${HOME}/.local/state/deskwave"
install -m 0644 "${unit_source}" "${unit_directory}/deskwave-host.service"
systemctl --user daemon-reload

if [[ "${1:-}" == "--enable" ]]; then
    # A user service is otherwise stopped when the last login session closes,
    # which prevents it from starting on the next system boot before login.
    # Enabling lingering makes the user's systemd manager boot-persistent.
    loginctl enable-linger "$(id -un)"
    systemctl --user enable --now deskwave-host.service
else
    echo "Installed deskwave-host.service"
    echo "Enable it with: systemctl --user enable --now deskwave-host.service"
    echo "For boot-before-login startup, run this script with --enable."
fi
