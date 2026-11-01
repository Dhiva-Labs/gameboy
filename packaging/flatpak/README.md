# Flatpak

`com.dhivalabs.gameboy.yml` packages gameboy for Flatpak on the
`org.freedesktop.Platform` 25.08 runtime, which carries SDL2.

## How CI builds it

The `distros` job in `.github/workflows/ci.yml` lays out the installed tree
from the `gameboy` binary the build job compiled and tested
(`packaging/linux-distros/make-tree.sh`). The `flatpak` job unpacks that tree
into `packaging/flatpak/tree/` and runs flatpak-builder on this manifest in
the `ghcr.io/flathub-infra/flatpak-github-actions:freedesktop-25.08`
container, once per architecture (x86_64 and aarch64, each on a native
runner). The result is a single-file bundle, `gameboy-<version>-<arch>.flatpak`,
whose runtime repository is Flathub.

The `flatpak-test` job then installs that bundle on a plain Ubuntu runner,
pulling the runtime from Flathub, and runs it in the sandbox
(`test-bundle.sh`): ldd of `/app/bin/gameboy`, the repository's frontend smoke
test, and the X11 window under Xvfb.

## Installing the bundle

```sh
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user ./gameboy-<version>-x86_64.flatpak
flatpak run com.dhivalabs.gameboy ~/roms/game.gb
```

The app can read your home directory (read-only) so ROM paths given on the
command line work; a `.gb` opened from a file manager is passed through the
document portal.

## Flathub

Flathub builds from source, so a submission would replace the `tree`
directory source with the repository at a release tag and build it with
CMake inside the manifest (SDL2 is already in the runtime), and add a
`<releases>` entry to the metainfo. The repository also needs a license
before it can be submitted.
