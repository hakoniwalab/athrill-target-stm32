#!/bin/sh
# Coverage of the CPU by the CTest tests (clang source-based coverage):
#   1. code: lines, branches and functions of the executors, the glue, the
#      M-profile implementation and athrill-arm-common's pseudo-code;
#   2. encodings: which instruction encodings with an executor the tests
#      run (tools/encoding_coverage.py).
#   tools/coverage.sh [build dir]
set -e
cd "$(dirname "$0")/.."
build="${1:-build-coverage}"
common="${ATHRILL_ARM_COMMON_DIR:-../athrill-arm-common}"
if command -v xcrun >/dev/null 2>&1; then
  profdata="xcrun llvm-profdata"
  cov="xcrun llvm-cov"
else
  profdata="llvm-profdata"
  cov="llvm-cov"
fi
cmake -S . -B "$build" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug \
  -DATHRILL_ENABLE_LTO=OFF -DATHRILL_STM32_COVERAGE=ON -DBUILD_TESTING=ON >/dev/null
cmake --build "$build" --parallel >/dev/null
rm -f "$build"/cov-*.profraw
(cd "$build" && LLVM_PROFILE_FILE="$PWD/cov-%p.profraw" ctest --output-on-failure >/dev/null)
$profdata merge -o "$build/ctest.profdata" "$build"/cov-*.profraw
files="src/cpu/thumb_exec.c src/cpu/thumb_exec16.c src/cpu/thumb_exec32.c src/cpu/thumb_glue.h src/cpu/arm_target_profile.h $common/include/arm_common/arm_helpers.h $common/include/arm_common/arm_pseudo_*.h"
echo "== code"
$cov report -instr-profile="$build/ctest.profdata" "$build/athrill3" $files
$cov export -format=text -skip-expansions -instr-profile="$build/ctest.profdata" "$build/athrill3" \
  src/cpu/thumb_exec16.c src/cpu/thumb_exec32.c > "$build/coverage.json"
echo "== encodings"
python3 tools/encoding_coverage.py "$build/coverage.json"
