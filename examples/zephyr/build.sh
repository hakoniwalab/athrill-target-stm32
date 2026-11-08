#!/bin/sh
# Builds Zephyr's hello_world, synchronization and philosophers samples for
# Zephyr's nucleo_f767zi and nucleo_h743zi boards, unchanged, in Docker:
#   examples/zephyr/build.sh
# Sources at the revisions of revisions.txt and the Zephyr SDK's Arm
# compiler (checked against the SDK's sha256.sum) go into work/; the ELFs
# into work/elf/<sample>-<board>.elf. Needs Docker, git and curl.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$HERE/work
rev() { awk -v k="$1" '$1 == k { print $2 }' "$HERE/revisions.txt"; }
SDK=$(rev sdk)
mkdir -p "$WORK/src/modules/hal" "$WORK/sdk" "$WORK/elf"

fetch() {	# repository, directory, revision
	if [ "$(git -C "$2" rev-parse HEAD 2>/dev/null || true)" = "$3" ]; then
		return
	fi
	rm -rf "$2"
	git init -q "$2"
	git -C "$2" fetch -q --depth 1 "https://github.com/zephyrproject-rtos/$1" "$3"
	git -C "$2" checkout -q FETCH_HEAD
}
echo "sources (revisions.txt)"
fetch zephyr "$WORK/src/zephyr" "$(rev zephyr)"
fetch cmsis "$WORK/src/modules/hal/cmsis" "$(rev cmsis)"
fetch CMSIS_6 "$WORK/src/modules/hal/cmsis_6" "$(rev cmsis_6)"
fetch hal_stm32 "$WORK/src/modules/hal/stm32" "$(rev hal_stm32)"

# The SDK for the Docker host's architecture (Linux aarch64 or x86_64).
case $(docker info --format '{{.Architecture}}') in
aarch64|arm64) HOST=linux-aarch64 ;;
*) HOST=linux-x86_64 ;;
esac
DIR=$WORK/sdk/zephyr-sdk-$SDK-$HOST
if [ ! -x "$DIR/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ]; then
	echo "Zephyr SDK $SDK ($HOST, minimal + arm-zephyr-eabi)"
	URL=https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v$SDK
	# a file already here is kept (its hash is checked below)
	get() { [ -s "$1" ] || curl -fsSLO --retry 5 --retry-all-errors "$URL/$1"; }
	cd "$WORK/sdk"
	MIN=zephyr-sdk-${SDK}_${HOST}_minimal.tar.xz
	ARM=toolchain_gnu_${HOST}_arm-zephyr-eabi.tar.xz
	get sha256.sum
	get $MIN
	get $ARM
	grep -E " ($MIN|$ARM)\$" sha256.sum | sha256sum -c - 2>/dev/null || grep -E " ($MIN|$ARM)\$" sha256.sum | shasum -a 256 -c -
	rm -rf "$DIR" "zephyr-sdk-$SDK"
	tar -xf "$MIN"
	mkdir -p "zephyr-sdk-$SDK/gnu"
	(cd "zephyr-sdk-$SDK/gnu" && tar -xf "../../$ARM")
	(cd "zephyr-sdk-$SDK" && ln -sf gnu/* .)
	mv "zephyr-sdk-$SDK" "$DIR"
	rm -f "$MIN" "$ARM"
	cd "$HERE"
fi

docker build -q -t athrill-zephyr-build "$HERE" > /dev/null
for board in nucleo_f767zi nucleo_h743zi; do
	for sample in hello_world synchronization philosophers; do
		echo "build $sample for $board"
		docker run --rm -v "$WORK/src:/src" -v "$DIR:/sdk:ro" athrill-zephyr-build sh -c "
			set -e
			git config --global --add safe.directory '*'
			export ZEPHYR_BASE=/src/zephyr ZEPHYR_SDK_INSTALL_DIR=/sdk ZEPHYR_TOOLCHAIN_VARIANT=zephyr
			rm -rf /src/build/$sample-$board
			cmake -S /src/zephyr/samples/$sample -B /src/build/$sample-$board -GNinja \
				-DBOARD=$board -DZEPHYR_MODULES='/src/modules/hal/cmsis;/src/modules/hal/cmsis_6;/src/modules/hal/stm32' > /dev/null
			ninja -C /src/build/$sample-$board > /dev/null"
		cp "$WORK/src/build/$sample-$board/zephyr/zephyr.elf" "$WORK/elf/$sample-$board.elf"
	done
done
ls -l "$WORK/elf"
