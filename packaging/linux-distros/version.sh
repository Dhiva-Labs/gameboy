#!/usr/bin/env bash
# Prints the package version. gameboy has no version in its sources: a release
# takes it from its tag (v1.2.3 -> 1.2.3); every other run builds
# 0.0.0.git<commit>, which is never published.
set -euo pipefail
if [ "${GITHUB_EVENT_NAME:-}" = release ]; then
  tag="${RELEASE_TAG:?set RELEASE_TAG to the release tag}"
  v="${tag#v}"
  # rpm and pacman versions may not contain '-'.
  [[ "$v" =~ ^[0-9][0-9A-Za-z.+_]*$ ]] || { echo "release tag $tag is not v<version>" >&2; exit 1; }
  echo "$v"
else
  # GITHUB_SHA in CI (some jobs check out without .git), else the checkout.
  sha="${GITHUB_SHA:-$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse HEAD)}"
  echo "0.0.0.git${sha:0:7}"
fi
