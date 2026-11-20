# Sourced by the docs' commands, from the directory that holds athrill-target-stm32:
#   . athrill-target-stm32/tools/versions.sh
# Sets the names and URLs of versions.txt, so no document writes a version.
_athrill_v() { awk -v k="$1" '$1 == k { print $2 }' athrill-target-stm32/versions.txt; }
ATHRILL_RELEASE=$(_athrill_v athrill-target-stm32)
ATHRILL_RELEASE_URL=https://github.com/hakoniwalab/athrill-target-stm32/releases/download/$ATHRILL_RELEASE
case "$(uname -s)" in
Darwin) ATHRILL_PLATFORM=macos-arm64; ATHRILL_SHA256="shasum -a 256" ;;
*) ATHRILL_PLATFORM=linux-x86_64; ATHRILL_SHA256=sha256sum ;;
esac
ATHRILL3_DIR=athrill3-$ATHRILL_RELEASE-$ATHRILL_PLATFORM
ATHRILL3=$ATHRILL3_DIR/athrill3
ZEPHYR_SAMPLES_RELEASE=$(_athrill_v zephyr-samples)
ZEPHYR_SAMPLES=zephyr-samples-$ZEPHYR_SAMPLES_RELEASE
ZEPHYR_SAMPLES_URL=https://github.com/hakoniwalab/athrill-target-stm32/releases/download/$ZEPHYR_SAMPLES_RELEASE
ATHRILL_COMMIT=$(_athrill_v athrill)
ATHRILL_ARM_COMMON_COMMIT=$(_athrill_v athrill-arm-common)
ATHRILL_DEVICE_COMMIT=$(_athrill_v athrill-device)
SLJIT_COMMIT=$(_athrill_v sljit)
HAKONIWA_DRONE_RELEASE=$(_athrill_v hakoniwa-drone-core)
case "$(uname -s)" in
Darwin) HAKONIWA_DRONE_ZIP=mac.zip; HAKONIWA_DRONE_BIN=mac/mac ;;
*) HAKONIWA_DRONE_ZIP=lnx.zip; HAKONIWA_DRONE_BIN=lnx/linux ;;
esac
HAKONIWA_DRONE_RELEASE_URL=https://github.com/toppers/hakoniwa-drone-core/releases/download/$HAKONIWA_DRONE_RELEASE
