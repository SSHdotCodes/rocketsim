#!/usr/bin/env bash
# Build the C++/WASM RocketSim into dist/. Pass "debug" for an assertions build.
set -euo pipefail
cd "$(dirname "$0")"
source ~/emsdk/emsdk_env.sh >/dev/null 2>&1

MODE="${1:-release}"
rm -rf dist && mkdir -p dist

COMMON=( -std=c++17 cpp/main.cpp -o dist/rocketsim.js
  -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
  -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 -sENVIRONMENT=web
  -sINITIAL_MEMORY=33554432 -sSTACK_SIZE=1048576 )

if [ "$MODE" = "debug" ]; then
  echo "[build] debug"
  em++ "${COMMON[@]}" -O1 -g -sASSERTIONS=1 -sGL_ASSERTIONS=1
else
  echo "[build] release"
  em++ "${COMMON[@]}" -O3 -flto -sASSERTIONS=0
fi

# stamp a unique build version so the CDN never serves a stale js/wasm pair
BUILD_V="$(date +%s)"
sed "s/__BUILD_V__/${BUILD_V}/g" shell/index.html > dist/index.html
echo "[build] version ${BUILD_V}"
ls -la dist/rocketsim.js dist/rocketsim.wasm dist/index.html
echo "[build] done"
