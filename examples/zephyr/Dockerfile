# What a Zephyr build needs besides the Zephyr SDK's compiler (build.sh mounts
# the SDK at /sdk): CMake, Ninja, the Devicetree compiler and Zephyr's Python
# build scripts.
FROM ubuntu:24.04
RUN apt-get update -qq \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
    cmake ninja-build device-tree-compiler cpp python3 python3-pyelftools \
    python3-yaml python3-packaging python3-jsonschema python3-pykwalify \
    git xz-utils file make gperf \
 && rm -rf /var/lib/apt/lists/*
