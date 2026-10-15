#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
build_dir="${1:-build-verify}"
if ! command -v meson >/dev/null 2>&1; then
  echo "meson is required" >&2
  exit 2
fi
if ! command -v ninja >/dev/null 2>&1; then
  echo "ninja is required" >&2
  exit 2
fi
if [ -f "$build_dir/build.ninja" ]; then
  meson setup --reconfigure "$build_dir" -Dbuildtype=debug
else
  meson setup "$build_dir" -Dbuildtype=debug
fi
meson compile -C "$build_dir"
meson test -C "$build_dir" --print-errorlogs --no-rebuild
