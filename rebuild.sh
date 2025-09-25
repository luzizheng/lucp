#!/usr/bin/env bash
set -euo pipefail

# 可环境变量覆盖
BUILD_TYPE=${BUILD_TYPE:-Release}
BUILD_DIR=${BUILD_DIR:-build}
INSTALL_PREFIX=${INSTALL_PREFIX:-/usr/local}   # 默认安装前缀
JOBS=${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}

echo ">>> Clean previous build artefacts"
if [[ -d "$BUILD_DIR" ]]; then
    rm -rf "$BUILD_DIR"/*
else
    mkdir -p "$BUILD_DIR"
fi

echo ">>> CMake configure  (BUILD_TYPE=$BUILD_TYPE, PREFIX=$INSTALL_PREFIX)"
cmake -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
      "$@"

echo ">>> CMake build"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

echo ">>> CMake install (may need sudo if PREFIX=/usr/local)"
# 如果目的地需要写权限，自动加 sudo；否则直接装
if [[ -w "$INSTALL_PREFIX" ]] || [[ "$INSTALL_PREFIX" == "$HOME"* ]]; then
    cmake --install "$BUILD_DIR"
else
    sudo cmake --install "$BUILD_DIR"
fi


echo ">>> All done. Libraries/headers installed to $INSTALL_PREFIX"