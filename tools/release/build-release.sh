#!/bin/sh
# Builds a release tarball of athrill3 from the commits of versions.txt:
#   tools/release/build-release.sh <workspace>
# <workspace> holds athrill-target-stm32 (this checkout, at the release tag),
# and next to it athrill, athrill-arm-common, athrill-device and sljit. The
# script checks each one out at its versions.txt commit (cloning it if it is
# missing), builds athrill3 with the JIT and tick skip, runs CTest, and writes
# <workspace>/dist/athrill3-<release>-<platform>.tar.gz with BUILD-INFO.txt,
# bindings/ and the licences. macOS arm64 (macOS 14 or later) or Linux x86_64.
# Needs: cmake, a C/C++ compiler, libyaml (static: Homebrew's libyaml on macOS,
# libyaml-dev on Linux), pkg-config, git.
set -eu
[ $# -eq 1 ] || { sed -n 2,11p "$0" >&2; exit 2; }
W=$(cd "$1" && pwd)
T=$W/athrill-target-stm32
[ -f "$T/versions.txt" ] || { echo "no $T/versions.txt" >&2; exit 1; }
v() { awk -v k="$1" '$1 == k { print $2 }' "$T/versions.txt"; }
REL=$(v athrill-target-stm32)

case "$(uname -s)-$(uname -m)" in
Darwin-arm64) PLATFORM=macos-arm64; DESC="macOS on Apple silicon (arm64), macOS 14 or later"
	EXTRA="-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0"
	YAML_LICENSE=$(brew --prefix libyaml)/License ;;
Linux-x86_64) PLATFORM=linux-x86_64; DESC="Linux x86_64, glibc $(ldd --version | head -1 | awk '{print $NF}') or later"
	EXTRA=""
	YAML_LICENSE=/usr/share/doc/libyaml-0-2/copyright ;;
*) echo "unsupported host $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

checkout() {	# <dir> <url> <commit>
	[ -d "$W/$1" ] || git clone -q "$2" "$W/$1"
	git -C "$W/$1" fetch -q origin
	git -C "$W/$1" checkout -q --detach "$3"
}
checkout athrill https://github.com/toppers/athrill.git "$(v athrill)"
checkout athrill-arm-common https://github.com/hakoniwalab/athrill-arm-common.git "$(v athrill-arm-common)"
checkout athrill-device https://github.com/hakoniwalab/athrill-device.git "$(v athrill-device)"
checkout sljit https://github.com/zherczeg/sljit.git "$(v sljit)"

B=$T/build-release-$PLATFORM
rm -rf "$B"
# shellcheck disable=SC2086
cmake -S "$T" -B "$B" -DCMAKE_BUILD_TYPE=Release -DATHRILL_DEVICE_STATIC_YAML=ON \
	-DATHRILL_ENABLE_JIT=ON -DATHRILL_DEVICE_ENABLE_TICK_SKIP=ON $EXTRA
cmake --build "$B" --parallel
# The release needs every test: ctest's exit status decides (a "99% tests passed" line is a failure).
if ! (cd "$B" && ctest --output-on-failure > ctest.log 2>&1); then
	grep -A20 'tests failed' "$B/ctest.log" >&2 || tail -40 "$B/ctest.log" >&2
	echo "ctest failed: no tarball ($B/ctest.log)" >&2
	exit 1
fi
TESTS=$(grep 'tests passed' "$B/ctest.log")

NAME=athrill3-$REL-$PLATFORM
P=$W/dist/$NAME
rm -rf "$P" "$P.tar.gz"
mkdir -p "$P"
cp "$B/athrill3" "$P/"
cp -R "$W/athrill-device/spec/bindings" "$P/bindings"
cp "$T/LICENSE" "$P/LICENSE"
cp "$W/athrill-arm-common/LICENSE" "$P/LICENSE.athrill-arm-common"
cp "$W/athrill-device/LICENSE" "$P/LICENSE.athrill-device"
cp "$W/athrill-device/LICENSES/BSD-2-Clause-libfdt.txt" "$P/LICENSE.libfdt"
cp "$YAML_LICENSE" "$P/LICENSE.libyaml"
cp "$W/sljit/LICENSE" "$P/LICENSE.sljit"
cat > "$P/NOTICE.athrill-core.txt" <<EOF
athrill3 contains the Athrill core (https://github.com/toppers/athrill,
commit $(git -C "$W/athrill" rev-parse HEAD)), which is licensed under the TOPPERS License Agreement:
https://www.toppers.jp/en/license.html
The copyright notices of the core are in its source files at that commit.
EOF
rev() { git -C "$W/$1" rev-parse HEAD; }
{
	echo "athrill3 for $DESC"
	echo "athrill-target-stm32  $(rev athrill-target-stm32) $REL"
	echo "athrill (core)        $(rev athrill)"
	echo "athrill-arm-common    $(rev athrill-arm-common)"
	echo "athrill-device        $(rev athrill-device) (models, bindings/)"
	echo "sljit                 $(rev sljit) (the JIT)"
	echo "libyaml               linked statically"
	echo "compiler              $(cc --version | head -1)"
	echo "build                 cmake -DCMAKE_BUILD_TYPE=Release -DATHRILL_DEVICE_STATIC_YAML=ON -DATHRILL_ENABLE_JIT=ON -DATHRILL_DEVICE_ENABLE_TICK_SKIP=ON $EXTRA"
	echo "tests                 ctest: $TESTS"
	echo "bindings              bindings/ next to athrill3 is found automatically (or set ATHRILL_DEVICE_BINDINGS_DIR)"
	echo "JIT and tick skip     built in, off unless the run's device configuration has DEVICE_CONFIG_STM32_JIT 1 and DEVICE_CONFIG_DEVICE_TICK_SKIP 1"
} > "$P/BUILD-INFO.txt"
(cd "$W/dist" && tar czf "$NAME.tar.gz" "$NAME")
cat "$P/BUILD-INFO.txt"
echo "$W/dist/$NAME.tar.gz"
