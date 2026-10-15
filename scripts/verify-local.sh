#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
build_dir="${1:-build-verify}"
mode="${RINPDF_VERIFY_MODE:-debug}"
if ! command -v meson >/dev/null 2>&1; then
  echo "meson is required" >&2
  exit 2
fi
if ! command -v ninja >/dev/null 2>&1; then
  echo "ninja is required" >&2
  exit 2
fi
case "$mode" in
  debug) sanitizer=none ;;
  asan) sanitizer=address,undefined ;;
  *) echo "RINPDF_VERIFY_MODE must be debug or asan" >&2; exit 2 ;;
esac
# Different build directories prevent stale sanitizer and non-sanitizer objects
# from being silently mixed.
if [ "$mode" = asan ] && [ "$build_dir" = build-verify ]; then
  build_dir=build-verify-asan
fi
if [ -f "$build_dir/build.ninja" ]; then
  meson setup --reconfigure "$build_dir" -Dbuildtype=debug -Db_sanitize="$sanitizer"
else
  meson setup "$build_dir" -Dbuildtype=debug -Db_sanitize="$sanitizer"
fi
# An older configured build may retain the former versioned shared
# library's SONAME. Reconfiguration is required after this build change.
# Meson will relink the test binaries against the unversioned librinpdf.so.
meson compile -C "$build_dir"
if [ "$#" -ge 2 ]; then
  meson test -C "$build_dir" --print-errorlogs --no-rebuild "$2"
else
  meson test -C "$build_dir" --print-errorlogs --no-rebuild
fi
echo "rinpdf verification passed: mode=$mode build_dir=$build_dir"
