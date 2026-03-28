#!/usr/bin/env bash
# 可重复：构建 fcitx5-windows → install 到 prefix → libime → chinese-addons（同一 prefix）。
# 依赖：MSYS2 Clang64、Ninja、Boost、libzstd、gettext；源码目录见环境变量。
# 详见 docs/PINYIN_WINDOWS.md
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE="${STAGE:-$ROOT/stage-pinyin}"
FCITX_BUILD="${FCITX_BUILD:-$ROOT/build-pinyin-fcitx}"
LIBIME_SRC="${LIBIME_SRC:-$ROOT/../libime}"
CHINESE_SRC="${CHINESE_SRC:-$ROOT/../fcitx5-chinese-addons}"
# MSYS2: default libime/chinese build dirs under /tmp (usually C:\msys64\tmp) to avoid Ninja
# "failed recompaction: Permission denied" on some Windows drives (Defender / indexing).
# Use uname, not only MSYSTEM: GitHub Actions bash often does not set MSYSTEM like an interactive shell.
_is_msys=0
case "$(uname -s 2>/dev/null)" in
  *MINGW* | MSYS_NT*) _is_msys=1 ;;
esac
if [[ $_is_msys -eq 1 ]]; then
  LIBIME_BUILD="${LIBIME_BUILD:-/tmp/fcitx5-pinyin-libime}"
  CHINESE_BUILD="${CHINESE_BUILD:-/tmp/fcitx5-pinyin-chinese}"
else
  LIBIME_BUILD="${LIBIME_BUILD:-$ROOT/build-pinyin-libime}"
  CHINESE_BUILD="${CHINESE_BUILD:-$ROOT/build-pinyin-chinese}"
fi
BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 8)}"

if [[ ! -f "$ROOT/CMakeLists.txt" ]]; then
  echo "ROOT invalid: $ROOT" >&2
  exit 1
fi
if [[ ! -f "$LIBIME_SRC/CMakeLists.txt" ]]; then
  echo "LIBIME_SRC missing: $LIBIME_SRC (clone https://github.com/fcitx/libime)" >&2
  exit 1
fi
if [[ ! -f "$CHINESE_SRC/CMakeLists.txt" ]]; then
  echo "CHINESE_SRC missing: $CHINESE_SRC (run scripts/prepare-pinyin-deps.sh or clone fcitx5-chinese-addons)" >&2
  exit 1
fi

# libime vendors KenLM as a Git submodule (src/libime/core/kenlm).
if [[ ! -f "$LIBIME_SRC/src/libime/core/kenlm/lm/build_binary_main.cc" ]]; then
  echo "==> KenLM submodule missing under libime; running: git submodule update --init --recursive"
  (cd "$LIBIME_SRC" && git submodule update --init --recursive)
fi

echo "==> Stage prefix: $STAGE"
mkdir -p "$STAGE"

echo "==> [1/3] fcitx5-windows configure + build + install"
cmake -S "$ROOT" -B "$FCITX_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX="$STAGE" \
  -DFCITX5_WINDOWS_BUILD_WIN32_IME=ON \
  "$@"
cmake --build "$FCITX_BUILD" -j"$JOBS"
cmake --install "$FCITX_BUILD" --prefix "$STAGE"

# Ninja runs some build steps via cmd.exe; the loader must find MinGW + fcitx DLLs (e.g. libFcitx5Utils.dll in $STAGE/bin).
if [[ $_is_msys -eq 1 ]]; then
  case "${MSYSTEM:-}" in
    CLANG64 | MINGW64 | UCRT64)
      export PATH="$STAGE/bin:/${MSYSTEM,,}/bin:/usr/bin${PATH:+:$PATH}"
      ;;
    *)
      # CI / non-interactive bash: MSYSTEM may be unset — still need the Clang64 toolchain on PATH.
      if [[ -d /clang64/bin ]]; then
        export PATH="$STAGE/bin:/clang64/bin:/usr/bin${PATH:+:$PATH}"
      elif [[ -d /mingw64/bin ]]; then
        export PATH="$STAGE/bin:/mingw64/bin:/usr/bin${PATH:+:$PATH}"
      else
        export PATH="$STAGE/bin${PATH:+:$PATH}"
      fi
      ;;
  esac
else
  export PATH="$STAGE/bin${PATH:+:$PATH}"
fi

echo "==> [2/3] libime"
cmake -S "$LIBIME_SRC" -B "$LIBIME_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX="$STAGE" \
  -DCMAKE_PREFIX_PATH="$STAGE" \
  -DENABLE_TEST=OFF \
  -DENABLE_TOOLS=ON
cmake --build "$LIBIME_BUILD" -j"$JOBS"
cmake --install "$LIBIME_BUILD" --prefix "$STAGE"
# Some layouts only install COMPONENT tools when requested explicitly.
cmake --install "$LIBIME_BUILD" --prefix "$STAGE" --component tools 2>/dev/null || true

# LibIME*Config / fcitx5-chinese-addons expect paths without .exe; Ninja on Windows still resolves
# D:/prefix/bin/libime_pinyindict. MinGW installs libime_*.exe — ensure extensionless copies exist.
# Use cmake -E copy on MSYS: plain cp to an extensionless name is flaky for Ninja/cmd.exe visibility.
copy_tool_no_ext() {
  local src="$1" dst="$2"
  if command -v cmake >/dev/null 2>&1; then
    cmake -E copy "$src" "$dst"
  else
    cp -f "$src" "$dst"
  fi
}

ensure_libime_tool_names() {
  echo "==> ensuring libime CLI tools (extensionless names for fcitx5-chinese-addons / Ninja)"
  local _tool
  for _tool in libime_pinyindict libime_tabledict libime_slm_build_binary libime_prediction libime_history libime_migrate_fcitx4_pinyin libime_migrate_fcitx4_table; do
    if [[ -f "$STAGE/bin/${_tool}" ]]; then
      continue
    fi
    if [[ -f "$STAGE/bin/${_tool}.exe" ]]; then
      copy_tool_no_ext "$STAGE/bin/${_tool}.exe" "$STAGE/bin/${_tool}"
      continue
    fi
    if [[ -f "$LIBIME_BUILD/bin/${_tool}.exe" ]]; then
      mkdir -p "$STAGE/bin"
      copy_tool_no_ext "$LIBIME_BUILD/bin/${_tool}.exe" "$STAGE/bin/${_tool}.exe"
      copy_tool_no_ext "$LIBIME_BUILD/bin/${_tool}.exe" "$STAGE/bin/${_tool}"
      continue
    fi
    if [[ -f "$LIBIME_BUILD/bin/${_tool}" ]]; then
      mkdir -p "$STAGE/bin"
      copy_tool_no_ext "$LIBIME_BUILD/bin/${_tool}" "$STAGE/bin/${_tool}"
      continue
    fi
    echo "error: ${_tool} not under $STAGE/bin or $LIBIME_BUILD/bin (ENABLE_TOOLS=Off or build failed?)" >&2
    ls -la "$STAGE/bin" 2>/dev/null || true
    ls -la "$LIBIME_BUILD/bin" 2>/dev/null || true
    exit 1
  done
  if [[ ! -f "$STAGE/bin/libime_pinyindict" ]]; then
    echo "error: STAGE/bin/libime_pinyindict still missing after copy step" >&2
    exit 1
  fi
}
ensure_libime_tool_names

echo "==> [3/3] fcitx5-chinese-addons"
# Stale build.ninja can keep old paths / deps from a previous failed run (e.g. self-hosted or retried step).
rm -rf "$CHINESE_BUILD"
cmake -S "$CHINESE_SRC" -B "$CHINESE_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX="$STAGE" \
  -DCMAKE_PREFIX_PATH="$STAGE" \
  -DENABLE_GUI=OFF \
  -DENABLE_OPENCC=OFF \
  -DENABLE_BROWSER=OFF \
  -DENABLE_CLOUDPINYIN=OFF \
  -DENABLE_TEST=OFF
cmake --build "$CHINESE_BUILD" -j"$JOBS"
cmake --install "$CHINESE_BUILD" --prefix "$STAGE"

echo ""
echo "Done. Portable root: $STAGE"
echo "  bin/     — Fcitx5.exe, *.dll, fcitx5-x86_64.dll (if built)"
echo "  lib/fcitx5/ — addons (expect pinyin.dll)"
echo "  share/fcitx5/ — data; profile example: share/fcitx5/profile.pinyin.example"
echo "Copy profile example to your config/fcitx5/profile or set FCITX_TS_IM=pinyin."
echo "TSF install (Admin PowerShell):"
echo "  pwsh -File scripts/install-fcitx5-ime.ps1 -Stage \"$STAGE\" -DeployDir \"C:/Fcitx5Portable\""
echo "Uninstall: scripts/uninstall-fcitx5-ime.ps1 -DeployDir \"...\" [-RemoveFiles] [-Force]"
echo "Or: deploy-ime-stage.ps1 / .sh (same as before; -Unregister = uninstall)"
