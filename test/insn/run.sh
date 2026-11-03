#!/bin/sh
# Runs an instruction test on Athrill and compares its output with the
# expected output made by QEMU (build.sh):
#   test/insn/run.sh <athrill3> <test>
set -e
athrill="$1"
dir="$(cd "$(dirname "$0")" && pwd)"
out="$(mktemp)"
trap 'rm -f "$out"' EXIT
"$athrill" -c1 -m "$dir/memory.txt" -d "$dir/device_config.txt" -t 10000000 "$dir/$2.elf" \
  | sed -n '/^DEBUG_FUNC_FT_LOG_SIZE/,$p' | sed '1d' | sed '/^end$/q' > "$out"
if diff -u "$dir/$2.expected" "$out"; then
  echo "$2: $(wc -l < "$out") lines as QEMU"
else
  echo "$2: differs from QEMU"
  exit 1
fi
