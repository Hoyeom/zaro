#!/bin/sh
# System packages needed to build ZARO on Ubuntu 24.04 (CI and tools/ci/linux-docker.sh).
# SDL3 needs the X11 and Wayland headers to build its video backends; everything else
# (SDL3, wgpu-native, Slang, enkiTS) is fetched by CMake.
set -e
SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO="sudo"
$SUDO apt-get update -qq
DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y -qq --no-install-recommends \
  build-essential cmake ninja-build python3 git ca-certificates pkg-config \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
  libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols \
  libegl-dev libgl-dev libdrm-dev libgbm-dev libudev-dev libdbus-1-dev
