# Regression reference check, sourced by the example checks (POSIX sh).
#
# The references live with the boards in athrill-device:
#   <athrill-device>/boards/<board>/regression/<check>.txt
# found through ATHRILL_DEVICE_DIR, else the athrill-device checkout next to
# this repository. A reference holds the firmware's SHA-256, the instruction
# count and the SHA-256 of result files:
#   firmware_sha256 <hex>
#   instructions <count>
#   sha256 <file in the run directory> <hex>
#   tick <device>          (one per line, in tick order)
# The values are compared only for that firmware. The tick order is the
# order the devices are ticked in: the target's TARGET_TICK lines, with
# "devicetree" replaced by athrill-device's DEVICE_TICK lines (paths), read
# from the run's log (stderr).

. "$(dirname "$0")/../boards.sh"

regression_reference() {	# board, check -> path
	echo "$(athrill_device_dir)/boards/$1/regression/$2.txt"
}

regression_sha256() {
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum "$1" | cut -d' ' -f1
	else
		shasum -a 256 "$1" | cut -d' ' -f1
	fi
}

# The tick order of a run, one device per line, from its log.
regression_tick_order() {
	awk '
		/^DEVICE_TICK / { sub(/.* path=/, ""); dt[n++] = $0; next }
		/^TARGET_TICK / { sub(/.* name=/, ""); target[m++] = $0 }
		END {
			for (i = 0; i < m; i++) {
				if (target[i] == "devicetree") {
					for (j = 0; j < n; j++) print dt[j]
				} else {
					print target[i]
				}
			}
		}' "$1"
}

# regression_check reference elf instructions run-directory [log]
# Prints one [PASS]/[FAIL]/[SKIP] line per value; returns 1 on a FAIL.
# log: the run's stderr, for the tick order.
regression_check() {
	ref=$1
	failed=0
	if [ ! -f "$ref" ]; then
		echo "  [SKIP] regression reference: no $ref"
		return 0
	fi
	want=$(awk '$1 == "firmware_sha256" { print $2 }' "$ref")
	have=$(regression_sha256 "$2")
	if [ "$want" != "$have" ]; then
		echo "  [SKIP] regression reference is for firmware $want, this is $have"
		return 0
	fi
	want=$(awk '$1 == "instructions" { print $2 }' "$ref")
	if [ "$want" = "$3" ]; then
		echo "  [PASS] instructions = reference ($3)"
	else
		echo "  [FAIL] instructions ${3:-missing}, reference $want"
		failed=1
	fi
	for entry in $(awk '$1 == "sha256" { print $2 "=" $3 }' "$ref"); do
		name=${entry%%=*}
		want=${entry#*=}
		if [ -f "$4/$name" ] && [ "$(regression_sha256 "$4/$name")" = "$want" ]; then
			echo "  [PASS] $name = reference"
		else
			echo "  [FAIL] $name differs from the reference"
			failed=1
		fi
	done
	if grep -q '^tick ' "$ref"; then
		want=$(awk '$1 == "tick" { print $2 }' "$ref")
		if [ -z "${5:-}" ] || [ ! -f "$5" ]; then
			echo "  [FAIL] tick order: no log"
			failed=1
		else
			have=$(regression_tick_order "$5")
			if [ "$want" = "$have" ]; then
				echo "  [PASS] tick order = reference ($(echo "$have" | wc -l | tr -d ' ') devices)"
			else
				echo "  [FAIL] tick order differs from the reference (position: reference / this run):"
				printf '%s\n' "$want" > "$4/tick-order.reference"
				printf '%s\n' "$have" > "$4/tick-order.run"
				awk 'NR == FNR { ref[FNR] = $0; n = FNR; next }
					{ run[FNR] = $0; m = FNR }
					END {
						for (i = 1; i <= (n > m ? n : m); i++)
							if (ref[i] != run[i])
								printf "    %d: %s / %s\n", i - 1,
									(i in ref) ? ref[i] : "-", (i in run) ? run[i] : "-"
					}' "$4/tick-order.reference" "$4/tick-order.run"
				failed=1
			fi
		fi
	fi
	return $failed
}
