#!/usr/bin/env bash
# One-shot configure + build: Fcitx5 (core/utils) + TSF IME from repo root.
# Run inside MSYS2 UCRT64 or Clang64 (Ninja + Clang on PATH).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-msys}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 8)}"

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DFCITX5_WINDOWS_BUILD_WIN32_IME=ON \
  "$@"

cmake --build "$BUILD_DIR" -j"$JOBS"

if [ "${SKIP_TEST:-0}" != 1 ]; then
  ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

echo "IME + Core built. Fcitx5Core/Fcitx5Utils are copied next to fcitx5-x86_64.dll (POST_BUILD)."
echo "Portable layout: cmake --install \"$BUILD_DIR\" --prefix <prefix> (IME + Core/Utils go to CMAKE_INSTALL_BINDIR)."
echo "Pinyin: see docs/PINYIN_WINDOWS.md (libime + fcitx5-chinese-addons into same prefix after install)."
