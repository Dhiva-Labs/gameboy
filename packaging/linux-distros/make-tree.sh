#!/usr/bin/env bash
# Lays out the installed file tree that pack.sh and the Flatpak are built
# from, using the gameboy binary the build job compiled and tested (there is no
# .deb to unpack).
#
# usage: make-tree.sh <gameboy binary> <root>
set -euo pipefail
[ $# -eq 2 ] || { echo "usage: $0 <gameboy binary> <root>" >&2; exit 2; }
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bin="$1"
root="$2"

install -Dm755 "$bin" "$root/usr/bin/gameboy"
install -Dm644 "$repo/packaging/linux/gameboy.desktop" "$root/usr/share/applications/gameboy.desktop"
install -Dm644 "$repo/packaging/linux/gameboy.png" "$root/usr/share/icons/hicolor/256x256/apps/gameboy.png"
install -Dm644 "$repo/README.md" "$root/usr/share/doc/gameboy/README.md"
find "$root" | sort
