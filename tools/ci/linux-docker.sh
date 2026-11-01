#!/bin/sh
# Builds and tests ZARO in a clean Ubuntu 24.04 container, like the Linux CI job.
# Usage (from the repository root): sh tools/ci/linux-docker.sh [--clang]
#   --clang  build with Clang instead of GCC, to catch Clang-only warnings (the macOS CI job
#            compiles with Apple Clang) before a pull request.
# The working tree is copied into the container (build/, .cache/ and .git/ excluded), so
# uncommitted changes are tested and the host build directories are not touched.
set -e
compiler=gcc
for arg in "$@"; do
  case "$arg" in
    --clang) compiler=clang ;;
    *) echo "linux-docker.sh: unknown option '$arg' (usage: linux-docker.sh [--clang])" >&2; exit 2 ;;
  esac
done
# Git Bash on Windows: pass a Windows path to docker and stop MSYS from rewriting /src paths.
src="$(pwd -W 2>/dev/null || pwd)"
export MSYS_NO_PATHCONV=1
docker run --rm -e ZARO_COMPILER="$compiler" -v "$src:/src:ro" ubuntu:24.04 sh -c '
  set -e
  sh /src/tools/ci/install-linux-deps.sh > /dev/null
  if [ "$ZARO_COMPILER" = clang ]; then
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends clang > /dev/null
    export CC=clang CXX=clang++
  fi
  mkdir /work
  cd /src && tar --exclude=./build --exclude=./.cache --exclude=./.git -cf - . | tar -xf - -C /work
  cd /work && python3 tools/build.py debug --test --no-gpu-tests --engine-budget=120
'
