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
# The values are compared only for that firmware.

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

# regression_check reference elf instructions run-directory
# Prints one [PASS]/[FAIL]/[SKIP] line per value; returns 1 on a FAIL.
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
	return $failed
}
