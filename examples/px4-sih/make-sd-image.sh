#!/bin/sh
# Makes sd.img, the emulated microSD card: 1 GB, FAT32 without a partition
# table (the PX4 logs, the mission data and a parameter backup go there).
# The image is made under another name and renamed only when it is complete.
set -e
cd "$(dirname "$0")"
if [ -e sd.img ]; then
	echo "sd.img exists"
	exit 0
fi
rm -f sd.img.new
case "$(uname -s)" in
Darwin)
	mkfile -n 1g sd.img.new
	DISK=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount sd.img.new 2>/dev/null | awk '{print $1}')
	if ! newfs_msdos -F 32 -v PX4SD "$DISK" > /dev/null; then
		hdiutil detach "$DISK" > /dev/null 2>&1
		rm -f sd.img.new
		exit 1
	fi
	hdiutil detach "$DISK" > /dev/null 2>&1
	;;
*)
	# Linux and WSL2: mkfs.vfat of dosfstools
	if ! command -v mkfs.vfat > /dev/null 2>&1; then
		echo "mkfs.vfat not found: install dosfstools (sudo apt install dosfstools)" >&2
		exit 1
	fi
	truncate -s 1G sd.img.new
	if ! mkfs.vfat -F 32 -n PX4SD sd.img.new > /dev/null; then
		rm -f sd.img.new
		exit 1
	fi
	;;
esac
mv sd.img.new sd.img
echo "sd.img: 1 GB FAT32"
