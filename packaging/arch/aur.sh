#!/usr/bin/env bash
# Renders the AUR PKGBUILD for one release and proves it builds.
#
# usage: aur.sh <gameboy-linux-x64.tar.gz> <gameboy-linux-arm64.tar.gz> <outdir>
#
# Fills PKGBUILD.in with the version, the release download URL and the
# sha256 of both release archives and of the packaging files the PKGBUILD
# fetches from the tag, then, in an archlinux:latest container, generates
# .SRCINFO, runs makepkg on the x86_64 sources (all of them are placed next
# to the PKGBUILD, so makepkg verifies the checksums without downloading),
# installs the result and runs it. Writes <name>-bin-<version>-aur.tar.gz
# holding PKGBUILD and .SRCINFO, ready to push to the AUR.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
[ $# -eq 3 ] || { echo "usage: $0 <x64.tar.gz> <arm64.tar.gz> <outdir>" >&2; exit 2; }
tar_x64="$(readlink -f "$1")"; tar_arm64="$(readlink -f "$2")"
mkdir -p "$3"; out="$(cd "$3" && pwd)"
# shellcheck source=/dev/null
. "$here/../linux-distros/distro.conf"
: "${VERSION:?set VERSION}"
tag="${TAG_PREFIX-v}$VERSION"
sha() { sha256sum "$1" | cut -d' ' -f1; }

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
deps=""; for d in "${ARCH_DEPENDS[@]}"; do deps+="'$d' "; done
sed -e "s|@NAME@|$NAME|g" -e "s|@VERSION@|$VERSION|g" \
    -e "s|@SUMMARY@|${SUMMARY//\'/}|g" -e "s|@HOMEPAGE@|$HOMEPAGE|g" \
    -e "s|@LICENSE@|$LICENSE|g" -e "s|@ARCH_DEPENDS@|${deps% }|g" \
    -e "s|@GITHUB_REPO@|$GITHUB_REPO|g" -e "s|@TAG@|$tag|g" \
    -e "s|@RELEASE_URL@|https://github.com/$GITHUB_REPO/releases/download/$tag|g" \
    -e "s|@SHA_DESKTOP@|$(sha "$repo/packaging/linux/gameboy.desktop")|g" \
    -e "s|@SHA_ICON@|$(sha "$repo/packaging/linux/gameboy.png")|g" \
    -e "s|@SHA_X64@|$(sha "$tar_x64")|g" -e "s|@SHA_ARM64@|$(sha "$tar_arm64")|g" \
    "$here/PKGBUILD.in" > "$work/PKGBUILD"
if grep -n '@[A-Z0-9_]*@' "$work/PKGBUILD"; then echo "unfilled placeholder" >&2; exit 1; fi
cp "$tar_x64" "$work/$NAME-bin-$VERSION-x86_64.tar.gz"
cp "$repo/packaging/linux/gameboy.desktop" "$repo/packaging/linux/gameboy.png" "$work/"

docker run --rm -v "$work:/build" archlinux:latest bash -euo pipefail -c '
  pacman -Syu --noconfirm --needed -q base-devel namcap
  useradd -m builder
  chown -R builder /build
  cd /build
  su builder -c "makepkg --printsrcinfo > .SRCINFO"
  su builder -c "makepkg --noconfirm --nodeps -f"
  namcap PKGBUILD
  pacman -U --noconfirm ./*-x86_64.pkg.tar.zst
  namcap ./*-x86_64.pkg.tar.zst
  pacman -Qlq '"$NAME"'-bin
  ldd /usr/bin/'"$NAME"'
  if ldd /usr/bin/'"$NAME"' | grep "not found"; then exit 1; fi
  cat .SRCINFO
'
tar -C "$work" -czf "$out/$NAME-bin-$VERSION-aur.tar.gz" PKGBUILD .SRCINFO
ls -l "$out/$NAME-bin-$VERSION-aur.tar.gz"
