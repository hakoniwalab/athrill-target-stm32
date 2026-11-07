#!/bin/sh
# Runs an instruction test on Athrill and compares its output with the
# expected output made by QEMU (build.sh), optionally in a mode: fast (the
# default: the host FPU where exact and the fast executors) or strict (the
# Arm pseudo-code only: DEVICE_CONFIG_ARM_FP_MODE and _EXEC_MODE strict):
#   test/insn/run.sh <athrill3> <test> [fast|strict]
# INSN_DEVICE_TREE: the board DTB that provides the console USART3 (CTest sets it).
set -e
athrill="$1"
dir="$(cd "$(dirname "$0")" && pwd)"
out="$(mktemp "${PWD}/insn-output.XXXXXX")"
config="$(mktemp "${PWD}/device-config.XXXXXX")"
trap 'rm -f "$out" "$config"' EXIT
cp "$dir/device_config.txt" "$config"
# the console USART is an athrill-device model: a board Devicetree provides it
if [ -n "${INSN_DEVICE_TREE:-}" ]; then
  echo "DEVICE_CONFIG_DEVICE_TREE $INSN_DEVICE_TREE" >> "$config"
fi
if [ -n "$3" ]; then
  echo "DEVICE_CONFIG_ARM_FP_MODE $3" >> "$config"
  echo "DEVICE_CONFIG_ARM_EXEC_MODE $3" >> "$config"
fi
"$athrill" -c1 -m "$dir/memory.txt" -d "$config" -t 10000000 "$dir/$2.elf" \
  | sed -n '/^DEBUG_FUNC_FT_LOG_SIZE/,$p' | sed '1d' | sed '/^end$/q' > "$out"
if diff -u "$dir/$2.expected" "$out"; then
  echo "$2 ${3:-fast}: $(wc -l < "$out") lines as QEMU"
else
  echo "$2: differs from QEMU"
  exit 1
fi
