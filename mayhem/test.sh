#!/usr/bin/env bash
#
# mayhem/test.sh — RUN the functional suite mayhem/build.sh built into build-tests/. exit 0 = pass.
# NotepadNext has no tests of its own; the suite is the upstream tests of the libraries it vendors
# and runs on every document (see mayhem/tests/CMakeLists.txt):
#   * lexilla/<lang>  one test per example directory: TestLexers lexes + folds every example and
#                     diffs against the committed .styled/.folded golden files; the test also
#                     requires the expected number of "Lexing <file>" lines.
#   * lexilla-detects-diff  known-answer negative test: a copy of the cpp examples with one golden
#                     .styled file altered MUST make TestLexers fail.
#   * scintilla-unit  Scintilla's Catch unit tests; requires Catch's "All tests passed" summary.
#                     Its "Document" case is excluded: it fails on pristine upstream (see below).
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

TL="$SRC/build-tests/TestLexers"
UT="$SRC/build-tests/unitTest"
for b in "$TL" "$UT"; do
  [ -x "$b" ] || { echo "missing $b — run mayhem/build.sh first" >&2; emit_ctrf "notepadnext-vendored-suites" 0 1; exit 1; }
done

passed=0; failed=0
pass() { passed=$((passed+1)); echo "PASS $1"; }
fail() { failed=$((failed+1)); echo "FAIL $1"; }

EX="$SRC/thirdparty/lexilla/test/examples"
work="$(mktemp -d "${TMPDIR:-/tmp}/nn-test.XXXXXX")"
trap 'rm -rf "$work"' EXIT

# TestLexers locates lexilla by walking up from its cwd; examples are passed by directory name.
for d in "$EX"/*/; do
  lang="$(basename "$d")"
  expected=$(find "$d" -maxdepth 1 -type f ! -name '*.styled' ! -name '*.folded' ! -name '*.properties' ! -name '*.new' | wc -l)
  out="$(cd "$SRC/thirdparty/lexilla/test" && "$TL" "$lang" 2>&1)"; rc=$?
  got=$(printf '%s\n' "$out" | grep -c '^Lexing ' || true)
  if [ "$rc" -eq 0 ] && [ "$got" -eq "$expected" ] && [ "$expected" -gt 0 ]; then
    pass "lexilla/$lang ($got files)"
  else
    fail "lexilla/$lang (rc=$rc, lexed $got of $expected)"; printf '%s\n' "$out" | tail -20
  fi
done

# Negative known-answer test: alter one golden style byte; TestLexers must report a difference.
mkdir -p "$work/neg"
cp -r "$EX/cpp" "$work/neg/cpp"
victim="$(ls "$work/neg/cpp"/*.styled | head -1)"
printf '{99}tampered{0}\n' >> "$victim"
out="$(cd "$SRC/thirdparty/lexilla/test" && "$TL" "$work/neg/cpp" 2>&1)"; rc=$?
if [ "$rc" -ne 0 ] && printf '%s\n' "$out" | grep -q 'has different styles'; then
  pass "lexilla-detects-diff"
else
  fail "lexilla-detects-diff (rc=$rc; a tampered golden file was not detected)"
fi

# "~Document" excludes Catch's "Document" test case: two of its RESearch UTF-8 assertions
# (testDocument.cxx:622 and :797) already fail on the pristine vendored tree at upstream 594670b.
out="$(cd "$work" && "$UT" "~Document" 2>&1)"; rc=$?
if [ "$rc" -eq 0 ] && printf '%s\n' "$out" | grep -q 'All tests passed'; then
  pass "scintilla-unit: $(printf '%s\n' "$out" | grep 'All tests passed')"
else
  fail "scintilla-unit (rc=$rc)"; printf '%s\n' "$out" | tail -30
fi

emit_ctrf "notepadnext-vendored-suites" "$passed" "$failed"
