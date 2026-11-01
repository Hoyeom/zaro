#!/bin/sh
# Builds and tests ZARO in a clean Ubuntu 24.04 container, like the Linux CI job.
# Usage (from the repository root): sh tools/ci/linux-docker.sh
# The working tree is copied into the container (build/, .cache/ and .git/ excluded), so
# uncommitted changes are tested and the host build directories are not touched.
set -e
# Git Bash on Windows: pass a Windows path to docker and stop MSYS from rewriting /src paths.
src="$(pwd -W 2>/dev/null || pwd)"
export MSYS_NO_PATHCONV=1
docker run --rm -v "$src:/src:ro" ubuntu:24.04 sh -c '
  set -e
  sh /src/tools/ci/install-linux-deps.sh > /dev/null
  mkdir /work
  cd /src && tar --exclude=./build --exclude=./.cache --exclude=./.git -cf - . | tar -xf - -C /work
  cd /work && python3 tools/build.py debug --test --no-gpu-tests --engine-budget=120
'
