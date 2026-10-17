# Real-PDF compatibility corpus

This runner is deliberately separate from the synthetic Meson unit tests.
Place PDFs under a local, untracked directory (e.g. `corpus/`) and add
a UTF-8 tab-separated manifest with four columns and no header:

```text
# path<TAB>open<TAB>page_count<TAB>render_first_page
corpus/known-vector.pdf	ok	ok	ok
corpus/text-only.pdf	ok	ok	unsupported
corpus/malformed.pdf	invalid_pdf	skip	skip
```

These example paths are placeholders, not fixtures included in this repository.
Set expected results only after inspecting each PDF and its intended structure.
An empty manifest fails; a file that is not present fails. No silent skips.

Build and run on WSL:

```sh
sh scripts/verify-local.sh build-verify-new
LD_LIBRARY_PATH="$PWD/build-verify-new" \
  "$PWD/build-verify-new/tests/rinpdf-corpus" corpus/manifest.tsv
```

Expected statuses:
- `open`: `ok`, `invalid_pdf`, `io_error`
- `page_count`: `ok`, `unsupported`, `skip`
- `render_first_page`: `ok`, `unsupported`, `skip`

Rendering checks a 64×64 RGBA buffer and verifies that `UNSUPPORTED`
does not alter the buffer. It does **not** prove pixel correctness; compare
images separately against MuPDF or PDFium with known reference fixtures.
Keep proprietary, confidential or licensed PDFs outside Git history.

## Reproducible combined verification

For the eight-file ZIP corpus distributed separately, extract the archive at
repository root, then run:

```sh
sh scripts/verify-corpus.sh manifest.tsv
```

This checks that the manifest is nonempty and every referenced fixture exists,
runs the Meson unit test suite, then checks the real PDFs with `rinpdf-corpus`.
A missing fixture, a failed unit test, or a mismatched expected status causes
a nonzero exit. This is a compatibility check, not a pixel-equivalence test.
