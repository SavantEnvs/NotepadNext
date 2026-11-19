#!/usr/bin/env bash
#
# mayhem/build.sh — build NotepadNext's fuzz harnesses and the functional test suite.
#
# Runs inside the commit image as `mayhem` in /mayhem; offline-safe and re-runnable (CMake
# reconfigures in place). Outputs:
#   build-mayhem/<target>, build-mayhem/<target>-standalone   fuzz targets + run-once reproducers
#   build-tests/TestLexers, build-tests/unitTest              functional suite run by mayhem/test.sh
# Both build dirs match upstream's .gitignore (/build*), so `git clean -ffdX` removes them.
#
# The fuzz build (mayhem/CMakeLists.txt) compiles the vendored Lua, Scintilla (Qt) and Lexilla with
# upstream's own CMake files plus the app's editor sources, all with $SANITIZER_FLAGS, $DEBUG_FLAGS
# (DWARF 3) and SanCov. Qt itself is Debian's (uninstrumented). Upstream's top-level build is not
# used: it builds the GUI executable and fetches four Qt UI libraries with CPM that no harness reaches.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export CC CXX
cd "$SRC"

# 1) Fuzz targets. SanCov (-fsanitize=fuzzer-no-link) on every object, so the library code — not
#    just the harness TU — is instrumented.
FUZZ_FLAGS="-O1 $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link"
# LeakSanitizer off at build time (SPEC §6.2 item 15): mayhem/lsan_off.cc, linked into every fuzz
# and -standalone binary by mayhem/CMakeLists.txt.
mkdir -p build-mayhem
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o build-mayhem/lsan_off.o
cmake -S mayhem -B build-mayhem -G Ninja \
    -DCMAKE_C_FLAGS="$FUZZ_FLAGS" \
    -DCMAKE_CXX_FLAGS="$FUZZ_FLAGS" \
    -DFUZZ_ENGINE="$LIB_FUZZING_ENGINE" \
    -DSTANDALONE_MAIN="$STANDALONE_FUZZ_MAIN" \
    -DLSAN_OFF_OBJ="$SRC/build-mayhem/lsan_off.o"
cmake --build build-mayhem -j"$MAYHEM_JOBS"

# 2) Functional suite (Lexilla golden-file lexer tests + Scintilla unit tests), normal flags.
cmake -S mayhem/tests -B build-tests -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS" \
    -DCMAKE_EXE_LINKER_FLAGS="$COVERAGE_FLAGS"
cmake --build build-tests -j"$MAYHEM_JOBS"
