#!/bin/sh
# Makes sd.img, the emulated microSD card: 1 GB, FAT32 without a partition
# table (the PX4 logs, the mission data and a parameter backup go there).
set -e
cd "$(dirname "$0")"
if [ -e sd.img ]; then
	echo "sd.img exists"
	exit 0
fi
case "$(uname -s)" in
Darwin)
	mkfile -n 1g sd.img
	DISK=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount sd.img 2>/dev/null | awk '{print $1}')
	newfs_msdos -F 32 -v PX4SD "$DISK" > /dev/null
	hdiutil detach "$DISK" > /dev/null 2>&1
	;;
*)
	# Linux and WSL2: mkfs.vfat from dosfstools
	truncate -s 1G sd.img
	mkfs.vfat -F 32 -n PX4SD sd.img > /dev/null
	;;
esac
echo "sd.img: 1 GB FAT32"
