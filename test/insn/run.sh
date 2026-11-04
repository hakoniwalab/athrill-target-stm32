#!/bin/sh
# Runs an instruction test on Athrill and compares its output with the
# expected output made by QEMU (build.sh), optionally in a floating-point
# mode (DEVICE_CONFIG_ARM_FP_MODE: fast, the default, or strict):
#   test/insn/run.sh <athrill3> <test> [fast|strict]
set -e
athrill="$1"
dir="$(cd "$(dirname "$0")" && pwd)"
out="$(mktemp)"
config="$(mktemp)"
trap 'rm -f "$out" "$config"' EXIT
cp "$dir/device_config.txt" "$config"
if [ -n "$3" ]; then
  echo "DEVICE_CONFIG_ARM_FP_MODE $3" >> "$config"
fi
"$athrill" -c1 -m "$dir/memory.txt" -d "$config" -t 10000000 "$dir/$2.elf" \
  | sed -n '/^DEBUG_FUNC_FT_LOG_SIZE/,$p' | sed '1d' | sed '/^end$/q' > "$out"
if diff -u "$dir/$2.expected" "$out"; then
  echo "$2 ${3:-fast}: $(wc -l < "$out") lines as QEMU"
else
  echo "$2: differs from QEMU"
  exit 1
fi
