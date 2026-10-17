#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
build_dir="${RINPDF_BUILD_DIR:-build-verify-new}"
manifest="${1:-manifest.tsv}"
if [ ! -f "$manifest" ]; then
  echo "Missing corpus manifest: $manifest" >&2
  echo "Extract rinpdf_test_corpus.zip into the repository root first." >&2
  exit 2
fi
# Avoid a misleading successful run when no actual PDFs are present.
awk -F '\t' '
  /^[[:space:]]*#/ || /^[[:space:]]*$/ {next}
  NF != 4 || $1 == "" {bad=1;next}
  {cases++;print $1}
  END {if (!cases || bad) exit 2}
' "$manifest" > /dev/null || {
  echo "Manifest has no valid cases or contains malformed rows" >&2
  exit 2
}
awk -F '\t' '
  /^[[:space:]]*#/ || /^[[:space:]]*$/ {next}
  {print $1}
' "$manifest" | while IFS= read -r path; do
  if [ ! -f "$path" ]; then
    echo "Missing PDF fixture: $path" >&2
    exit 2
  fi
done
sh scripts/verify-local.sh "$build_dir"
binary="$build_dir/tests/rinpdf-corpus"
if [ ! -x "$binary" ]; then
  echo "Corpus harness not found: $binary" >&2
  exit 2
fi
LD_LIBRARY_PATH="$PWD/$build_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  "$binary" "$manifest"
