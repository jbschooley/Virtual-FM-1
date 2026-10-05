#!/bin/sh
# Builds and tests on Linux in Docker (Ubuntu 24.04, Clang, as CI's linux job; not its packaging), from a
# Mac or anywhere Docker runs, without touching your build folders:
#   scripts/test-linux-docker.sh
# On Apple silicon it runs amd64 under emulation (about 20 minutes).
set -e
cd "$(dirname "$0")/.."
docker run --rm --platform linux/amd64 -v "$PWD":/src:ro ubuntu:24.04 bash -c '
set -eo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq cmake g++ clang ninja-build xvfb fluxbox libasound2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libxi-dev >/dev/null
mkdir /w && tar -C /src --exclude="./build*" -cf - . | tar -C /w -xf - && cd /w
CC=clang CXX=clang++ cmake -B lb -G Ninja -DCMAKE_BUILD_TYPE=Release -DFM1_COPY_PLUGIN=OFF -DFM1_BUILD_TOOLS=OFF >/dev/null
cmake --build lb
setsid Xvfb :99 -ac -screen 0 1280x1024x24 </dev/null >/dev/null 2>&1 &
sleep 2; DISPLAY=:99 setsid fluxbox </dev/null >/dev/null 2>&1 & sleep 2
DISPLAY=:99 ctest --test-dir lb --timeout 120 --output-on-failure
'
