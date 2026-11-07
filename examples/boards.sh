# Board Devicetrees for the examples, sourced (POSIX sh).
#
# The boards are described only by their Devicetree, kept in athrill-device
# (boards/<board>/<name>.dtb); there is no board code in this target.
# athrill-device is found through ATHRILL_DEVICE_DIR, else the checkout next
# to this repository. EXAMPLES is this examples directory; set it before
# sourcing when the script is not directly in examples/<example>/.

athrill_device_dir() {
	echo "${ATHRILL_DEVICE_DIR:-$(cd "${EXAMPLES:-$(dirname "$0")/..}/../.." && pwd)/athrill-device}"
}

board_dtb() {	# board, name -> path of the DTB
	echo "$(athrill_device_dir)/boards/$1/$2.dtb"
}
