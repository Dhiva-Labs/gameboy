#!/usr/bin/env bash
# Installs a gameboy Flatpak bundle the way a user would and runs it.
#
# usage: test-bundle.sh <bundle.flatpak> <rom dir>
#
# Runs on a plain Ubuntu runner with flatpak and Xvfb installed. The runtime
# comes from Flathub through the bundle's runtime repository, exactly as for
# `flatpak install --user ./gameboy-<version>-<arch>.flatpak`. Then, inside the
# sandbox: every library of /app/bin/gameboy resolves; the repository's own
# frontend smoke test (dummy SDL drivers, 120 frames, no error or warning
# output) passes; and the real X11 window stays up under Xvfb for 20 seconds, with
# the ROM read through the app's home:ro permission.
set -euo pipefail
[ $# -eq 2 ] || { echo "usage: $0 <bundle.flatpak> <rom dir>" >&2; exit 2; }
bundle="$(readlink -f "$1")"
roms="$(cd "$2" && pwd)"
app=com.dhivalabs.gameboy

flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y --noninteractive "$bundle"
flatpak info --user "$app"

echo "::group::ldd /app/bin/gameboy in the sandbox"
libs="$(flatpak run --command=ldd "$app" /app/bin/gameboy)"
echo "$libs"
echo "::endgroup::"
if grep 'not found' <<< "$libs"; then exit 1; fi

smoke="$HOME/gameboy-smoke"
mkdir -p "$smoke"
cp "$roms/dmg-acid2.gb" "$smoke/"

echo "::group::frontend smoke test (dummy SDL drivers)"
out="$(flatpak run --env=SDL_VIDEODRIVER=dummy --env=SDL_AUDIODRIVER=dummy \
  "$app" --scale 1 --frames 120 "$smoke/dmg-acid2.gb" 2>&1)"
printf '%s\n' "$out"
if grep -iE 'CPU stopped|error|warning' <<< "$out"; then exit 1; fi
echo "::endgroup::"

echo "::group::X11 window under Xvfb for 20 seconds"
cat > "$RUNNER_TEMP/smoke.sh" <<'EOS'
log="$RUNNER_TEMP/flatpak-smoke.log"
"$@" > "$log" 2>&1 &
pid=$!
for _ in $(seq 1 20); do
  kill -0 "$pid" 2> /dev/null || break
  sleep 1
done
if kill -0 "$pid" 2> /dev/null; then
  echo "still running after 20s"
  kill "$pid"
  ok=1
else
  wait "$pid" && rc=0 || rc=$?
  echo "exited early with status $rc"
  ok=0
fi
cat "$log"
if grep -E 'error while loading shared libraries|cannot open shared object|undefined symbol' "$log"; then ok=0; fi
[ "$ok" = 1 ]
EOS
xvfb-run -a -s '-screen 0 1280x800x24' dbus-run-session -- \
  bash "$RUNNER_TEMP/smoke.sh" flatpak run "$app" --mute "$smoke/dmg-acid2.gb"
echo "::endgroup::"
echo "flatpak: OK"
