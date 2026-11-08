#!/bin/sh
# Runs Zephyr's samples on both nucleo boards and checks their output:
#   examples/zephyr/check.sh [elf directory] [athrill3]
# The ELF directory holds <sample>-<board>.elf (build.sh writes work/elf; the
# release has the same files). Checks, per board:
#   hello_world      prints "Hello World! <board>/<SoC>"
#   synchronization  thread_a and thread_b take turns, a greeting every
#                    0.5 s: 9 greetings in 5 virtual seconds, alternating
#   philosophers     all six philosophers eat at least 10 times in 30
#                    virtual seconds
# Exit status 0 when all pass. A run's output is capped at 4 MB.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ELFS=${1:-$HERE/work/elf}
ATHRILL=${2:-$HERE/../../build/athrill3}
OUT=$HERE/work/check
mkdir -p "$OUT"
FAILED=0
result() {	# PASS/FAIL, text
	echo "  [$1] $2"
	[ "$1" = PASS ] || FAILED=1
}
run() {	# board, sample, virtual seconds -> output file
	"$HERE/run.sh" "$1" "$ELFS/$2-$1.elf" "$3" "$ATHRILL" 2>&1 | head -c 4000000 > "$OUT/$2-$1.txt"
	echo "$OUT/$2-$1.txt"
}
for board in nucleo_f767zi nucleo_h743zi; do
	case $board in
	nucleo_f767zi) soc=stm32f767xx ;;
	nucleo_h743zi) soc=stm32h743xx ;;
	esac
	echo "$board"
	f=$(run $board hello_world 1)
	if grep -aq "Hello World! $board/$soc" "$f"; then
		result PASS "hello_world: Hello World! $board/$soc"
	else
		result FAIL "hello_world: no greeting (see $f)"
	fi
	f=$(run $board synchronization 5)
	lines=$(grep -a -o 'thread_[ab]: Hello World from cpu 0' "$f" | cut -c8)
	n=$(echo "$lines" | grep -c .)
	turns=$(echo "$lines" | tr -d '\n')
	if [ "$n" = 9 ] && [ "$turns" = ababababa ]; then
		result PASS "synchronization: 9 greetings in 5 s, alternating"
	else
		result FAIL "synchronization: $n greetings ($turns), want 9 alternating (see $f)"
	fi
	f=$(run $board philosophers 30)
	ate=$(grep -a -o 'Philosopher [0-9] \[[^]]*\] *EATING' "$f" | awk '{ n[$2]++ } END { for (i = 0; i < 6; i++) if (n[i] >= 10) printf "%d", i }')
	if [ "$ate" = 012345 ]; then
		result PASS "philosophers: all six ate at least 10 times in 30 s"
	else
		result FAIL "philosophers: only $ate ate 10 times in 30 s (see $f)"
	fi
done
[ $FAILED = 0 ] && echo "ALL PASS" || echo "SOME CHECKS FAILED"
exit $FAILED
