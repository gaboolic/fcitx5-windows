#!/usr/bin/env bash
# Step 2 — 依赖链：libime → fcitx5-chinese-addons → 可选 table-extra / rime / lua（同一 STAGE）。
# 默认先执行 fcitx5-windows 安装到 STAGE。若已用 PowerShell 跑过 01-build-fcitx5-windows.ps1 装入同一 STAGE，可 export SKIP_FCITX_WINDOWS=1 跳过本节。
# 依赖：MSYS2 CLANG64（或 MINGW64/UCRT64）、Ninja、Boost、libzstd、gettext；Rime 等见 docs/PINYIN_WINDOWS.md。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE="${STAGE:-$ROOT/stage}"
FCITX_BUILD="${FCITX_BUILD:-$ROOT/build-pinyin-fcitx}"
LIBIME_SRC="${LIBIME_SRC:-$ROOT/../libime}"
CHINESE_SRC="${CHINESE_SRC:-$ROOT/../fcitx5-chinese-addons}"
TABLE_EXTRA_SRC="${TABLE_EXTRA_SRC:-$ROOT/../fcitx5-table-extra}"
RIME_SRC="${RIME_SRC:-$ROOT/../fcitx5-rime}"
LUA_SRC="${LUA_SRC:-$ROOT/../fcitx5-lua}"
# 与 fcitx5-prebuilder 一致：librime 源码 + 将 librime-lua 置于 plugins/lua 后 BUILD_MERGED_PLUGINS=ON。
LIBRIME_SRC="${LIBRIME_SRC:-$ROOT/../librime}"
LIBRIME_LUA_SRC="${LIBRIME_LUA_SRC:-$ROOT/../librime-lua}"
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
  TABLE_EXTRA_BUILD="${TABLE_EXTRA_BUILD:-/tmp/fcitx5-pinyin-table-extra}"
  RIME_BUILD="${RIME_BUILD:-/tmp/fcitx5-pinyin-rime}"
  LUA_BUILD="${LUA_BUILD:-/tmp/fcitx5-pinyin-lua}"
  LIBRIME_BUILD="${LIBRIME_BUILD:-/tmp/fcitx5-pinyin-librime}"
else
  LIBIME_BUILD="${LIBIME_BUILD:-$ROOT/build-pinyin-libime}"
  CHINESE_BUILD="${CHINESE_BUILD:-$ROOT/build-pinyin-chinese}"
  TABLE_EXTRA_BUILD="${TABLE_EXTRA_BUILD:-$ROOT/build-pinyin-table-extra}"
  RIME_BUILD="${RIME_BUILD:-$ROOT/build-pinyin-rime}"
  LUA_BUILD="${LUA_BUILD:-$ROOT/build-pinyin-lua}"
  LIBRIME_BUILD="${LIBRIME_BUILD:-$ROOT/build-pinyin-librime}"
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
  echo "CHINESE_SRC missing: $CHINESE_SRC (clone https://github.com/fcitx/fcitx5-chinese-addons next to fcitx5-windows, or set CHINESE_SRC)" >&2
  exit 1
fi

# libime vendors KenLM as a Git submodule (src/libime/core/kenlm).
if [[ ! -f "$LIBIME_SRC/src/libime/core/kenlm/lm/build_binary_main.cc" ]]; then
  echo "==> KenLM submodule missing under libime; running: git submodule update --init --recursive"
  (cd "$LIBIME_SRC" && git submodule update --init --recursive)
fi

echo "==> Stage prefix: $STAGE"
mkdir -p "$STAGE"

if [[ "${SKIP_FCITX_WINDOWS:-0}" != "1" ]]; then
  echo "==> [1/3] fcitx5-windows configure + build + install"
  cmake -S "$ROOT" -B "$FCITX_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$STAGE" \
    -DFCITX5_WINDOWS_BUILD_WIN32_IME=ON \
    "$@"
  cmake --build "$FCITX_BUILD" -j"$JOBS"
  cmake --install "$FCITX_BUILD" --prefix "$STAGE"
else
  echo "==> [1/3] skip fcitx5-windows (SKIP_FCITX_WINDOWS=1 — use scripts/01-build-fcitx5-windows.ps1 first if STAGE is empty)"
  if [[ ! -d "$STAGE/lib/cmake/Fcitx5Core" ]]; then
    echo "error: STAGE missing fcitx5 install (expected $STAGE/lib/cmake/Fcitx5Core). Run 01-build-fcitx5-windows.ps1 or unset SKIP_FCITX_WINDOWS." >&2
    exit 1
  fi
fi

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

if [[ $_is_msys -eq 1 ]]; then
  for _pc in /clang64/lib/pkgconfig /mingw64/lib/pkgconfig; do
    [[ -d "$_pc" ]] || continue
    case ":${PKG_CONFIG_PATH:-}:" in
      *":${_pc}:"*) ;;
      *) PKG_CONFIG_PATH="${PKG_CONFIG_PATH:+${PKG_CONFIG_PATH}:}${_pc}" ;;
    esac
  done
  export PKG_CONFIG_PATH
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

# Optional: extensionless copies for anything that still resolves the Unix path without .exe.
# Use cmake -E copy on MSYS where possible.
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
  if [[ ! -f "$STAGE/bin/libime_pinyindict" ]] && [[ ! -f "$STAGE/bin/libime_pinyindict.exe" ]]; then
    echo "error: libime_pinyindict (.exe or extensionless) missing under $STAGE/bin" >&2
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

# --- Optional: fcitx5-table-extra (extra table IMs / dicts; needs LibIMETable from stage) ---
if [[ -f "$TABLE_EXTRA_SRC/CMakeLists.txt" ]]; then
  echo "==> [4] fcitx5-table-extra"
  rm -rf "$TABLE_EXTRA_BUILD"
  cmake -S "$TABLE_EXTRA_SRC" -B "$TABLE_EXTRA_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$STAGE" \
    -DCMAKE_PREFIX_PATH="$STAGE"
  cmake --build "$TABLE_EXTRA_BUILD" -j"$JOBS"
  cmake --install "$TABLE_EXTRA_BUILD" --prefix "$STAGE"
else
  echo "==> skip fcitx5-table-extra (TABLE_EXTRA_SRC not set or missing CMakeLists.txt)"
fi

# --- Optional: fcitx5-rime (needs MSYS2 librime + rime-data; runtime DLLs from Clang64 bin) ---
mingw_dll_source_bin() {
  if [[ -d /clang64/bin ]]; then
    echo /clang64/bin
  elif [[ -d /mingw64/bin ]]; then
    echo /mingw64/bin
  else
    echo ""
  fi
}

vendor_rime_share_into_stage() {
  local rime_data="$STAGE/share/rime-data"
  if [[ -d "$rime_data" ]] && [[ -n "$(ls -A "$rime_data" 2>/dev/null)" ]]; then
    return 0
  fi
  local src
  for src in /clang64/share/rime-data /mingw64/share/rime-data; do
    if [[ -d "$src" ]]; then
      mkdir -p "$STAGE/share"
      cp -a "$src" "$rime_data"
      echo "==> vendored rime-data from $src -> $rime_data"
      return 0
    fi
  done
  return 1
}

# Rime 依赖 DLL（不含 librime 本体 — 本体由合并构建产出或由 MSYS2 包拷贝）。
copy_librime_dependency_dlls() {
  local _bin
  _bin="$(mingw_dll_source_bin)"
  [[ -n "$_bin" ]] || return 0
  mkdir -p "$STAGE/bin"
  local _f
  shopt -s nullglob
  for _f in "$_bin"/libopencc*.dll "$_bin"/libyaml-cpp.dll \
    "$_bin"/libleveldb.dll "$_bin"/libglog.dll "$_bin"/libmarisa*.dll \
    "$_bin"/libwinpthread-1.dll; do
    [[ -f "$_f" ]] && cp -f "$_f" "$STAGE/bin/"
  done
  for _f in "$_bin"/liblua-*.dll "$_bin"/liblua.dll "$_bin"/lua5*.dll; do
    [[ -f "$_f" ]] && cp -f "$_f" "$STAGE/bin/"
  done
  shopt -u nullglob
  if [[ -f "$_bin/rime_deployer.exe" ]]; then
    cp -f "$_bin/rime_deployer.exe" "$STAGE/bin/"
  fi
  # OpenCC config/data used by some Rime schemas
  if [[ -d /clang64/share/opencc ]] && [[ ! -d "$STAGE/share/opencc" ]]; then
    mkdir -p "$STAGE/share"
    cp -a /clang64/share/opencc "$STAGE/share/"
  elif [[ -d /mingw64/share/opencc ]] && [[ ! -d "$STAGE/share/opencc" ]]; then
    mkdir -p "$STAGE/share"
    cp -a /mingw64/share/opencc "$STAGE/share/"
  fi
}

copy_librime_dlls_from_msys() {
  local _bin _f
  _bin="$(mingw_dll_source_bin)"
  [[ -n "$_bin" ]] || return 0
  mkdir -p "$STAGE/bin"
  shopt -s nullglob
  for _f in "$_bin"/librime-*.dll; do
    [[ -f "$_f" ]] && cp -f "$_f" "$STAGE/bin/"
  done
  shopt -u nullglob
}

copy_librime_runtime_dlls() {
  copy_librime_dependency_dlls
  copy_librime_dlls_from_msys
}

# 参考 fcitx5-prebuilder scripts/librime.py：plugins/lua → librime-lua，BUILD_MERGED_PLUGINS=ON。
prepare_librime_lua_plugin_dir() {
  rm -rf "$LIBRIME_SRC/plugins/lua"
  cp -a "$LIBRIME_LUA_SRC" "$LIBRIME_SRC/plugins/lua"
}

mingw_toolchain_prefix() {
  if [[ -d /clang64 ]]; then
    echo /clang64
  elif [[ -d /mingw64 ]]; then
    echo /mingw64
  else
    echo ""
  fi
}

install_merged_librime_dll_into_bindir() {
  # 上游 install(TARGETS rime DESTINATION lib) 会把 DLL 放在 $STAGE/lib；运行时与 fcitx5 一致从 bin 加载。
  shopt -s nullglob
  local _f
  for _f in "$STAGE/lib"/librime-*.dll "$STAGE/lib"/librime.dll; do
    if [[ -f "$_f" ]]; then
      mkdir -p "$STAGE/bin"
      cp -f "$_f" "$STAGE/bin/"
      echo "==> copied $(basename "$_f") from lib/ to bin/"
    fi
  done
  shopt -u nullglob
}

build_merged_librime_with_lua() {
  local _prefix
  _prefix="$(mingw_toolchain_prefix)"
  [[ -n "$_prefix" ]] || return 1
  echo "==> [5a] librime + librime-lua (merged into librime DLL, same idea as fcitx5-prebuilder / fcitx5-macos deps)"
  prepare_librime_lua_plugin_dir || return 1
  rm -rf "$LIBRIME_BUILD"
  # 与 MSYS2 mingw-w64-librime PKGBUILD 对齐的 glog 宏；另强制 GFLAGS_IS_A_DLL 为 0/1，否则
  # gflags_declare.h 里 #if GFLAGS_IS_A_DLL 在 Clang+MinGW 下会因空宏报 invalid token。
  cmake -S "$LIBRIME_SRC" -B "$LIBRIME_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$STAGE" \
    -DCMAKE_PREFIX_PATH="${_prefix};${STAGE}" \
    -DBUILD_TEST=OFF \
    -DBUILD_MERGED_PLUGINS=ON \
    -DCMAKE_DLL_NAME_WITH_SOVERSION=ON \
    "-DCMAKE_CXX_FLAGS=-DNDEBUG -DGLOG_USE_GLOG_EXPORT -DGLOG_USE_GFLAGS -UGFLAGS_IS_A_DLL -DGFLAGS_IS_A_DLL=1" \
    || return 1
  cmake --build "$LIBRIME_BUILD" -j"$JOBS" || return 1
  cmake --install "$LIBRIME_BUILD" --prefix "$STAGE" || return 1
  install_merged_librime_dll_into_bindir
  export PKG_CONFIG_PATH="$STAGE/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
  return 0
}

if [[ -f "$RIME_SRC/CMakeLists.txt" ]]; then
  echo "==> [5] fcitx5-rime"
  if ! vendor_rime_share_into_stage; then
    echo "error: rime-data missing under stage and not found under /clang64 or /mingw64." >&2
    echo "  Install: pacman -S mingw-w64-clang-x86_64-librime-data (CLANG64; replaces old rime-data)" >&2
    exit 1
  fi
  if [[ $_is_msys -eq 1 ]] && [[ -f "$LIBRIME_SRC/CMakeLists.txt" ]] && [[ -f "$LIBRIME_LUA_SRC/CMakeLists.txt" ]]; then
    copy_librime_dependency_dlls
    if ! build_merged_librime_with_lua; then
      echo "warning: merged librime+librime-lua build failed; falling back to MSYS2 librime DLL (no Rime Lua in core)" >&2
      copy_librime_dlls_from_msys
    fi
  else
    if [[ $_is_msys -eq 1 ]]; then
      if [[ ! -f "$LIBRIME_SRC/CMakeLists.txt" ]]; then
        echo "==> skip merged librime build: LIBRIME_SRC missing or no CMakeLists.txt ($LIBRIME_SRC)" >&2
      fi
      if [[ ! -f "$LIBRIME_LUA_SRC/CMakeLists.txt" ]]; then
        echo "==> skip merged librime build: librime-lua missing or no CMakeLists.txt ($LIBRIME_LUA_SRC); Rime core will not embed Lua" >&2
      fi
      echo "==> falling back to copy_librime_runtime_dlls (MSYS2 prebuilt librime + deps)" >&2
    else
      echo "==> non-MSYS: using prebuilt librime DLLs only (set LIBRIME_SRC + LIBRIME_LUA_SRC on MSYS2 for merged lua)"
    fi
    copy_librime_runtime_dlls
  fi
  rm -rf "$RIME_BUILD"
  cmake -S "$RIME_SRC" -B "$RIME_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$STAGE" \
    -DCMAKE_PREFIX_PATH="$STAGE"
  cmake --build "$RIME_BUILD" -j"$JOBS"
  cmake --install "$RIME_BUILD" --prefix "$STAGE"
else
  echo "==> skip fcitx5-rime (RIME_SRC not set or missing CMakeLists.txt)"
fi

# --- Optional: fcitx5-lua (addon luaaddonloader; default USE_DLOPEN — ship liblua*.dll in bin) ---
copy_lua_runtime_dlls() {
  local _bin
  _bin="$(mingw_dll_source_bin)"
  [[ -n "$_bin" ]] || return 0
  mkdir -p "$STAGE/bin"
  local _f
  shopt -s nullglob
  for _f in "$_bin"/liblua-*.dll "$_bin"/liblua.dll "$_bin"/lua5*.dll; do
    [[ -f "$_f" ]] && cp -f "$_f" "$STAGE/bin/"
  done
  shopt -u nullglob
}

if [[ -f "$LUA_SRC/CMakeLists.txt" ]]; then
  echo "==> [6] fcitx5-lua"
  rm -rf "$LUA_BUILD"
  cmake -S "$LUA_SRC" -B "$LUA_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$STAGE" \
    -DCMAKE_PREFIX_PATH="$STAGE" \
    -DENABLE_TEST=OFF
  cmake --build "$LUA_BUILD" -j"$JOBS"
  cmake --install "$LUA_BUILD" --prefix "$STAGE"
  copy_lua_runtime_dlls
else
  echo "==> skip fcitx5-lua (LUA_SRC not set or missing CMakeLists.txt)"
fi

# MinGW produces libpinyin.dll (matches Library=libpinyin in pinyin.conf); MSVC layouts may use pinyin.dll.
_addon_dir="$STAGE/lib/fcitx5"
if [[ ! -f "$_addon_dir/libpinyin.dll" ]] && [[ ! -f "$_addon_dir/pinyin.dll" ]]; then
  echo "error: pinyin addon DLL missing under $_addon_dir (expected libpinyin.dll or pinyin.dll)" >&2
  echo "listing $_addon_dir:" >&2
  ls -la "$_addon_dir" 2>&1 >&2 || true
  echo "searching $STAGE for *pinyin*.dll:" >&2
  find "$STAGE" -iname '*pinyin*.dll' -print 2>/dev/null >&2 || true
  exit 1
fi

echo ""
echo "Done. Portable root: $STAGE"
echo "  bin/     — Fcitx5.exe, *.dll, fcitx5-x86_64.dll (if built), rime/lua runtime DLLs if those stages ran"
echo "  lib/fcitx5/ — addons (libpinyin.dll; optional librime.dll, libluaaddonloader.dll, …)"
echo "  share/fcitx5/ — data; profile example: share/fcitx5/profile.windows.example"
echo "  share/rime-data/ — when fcitx5-rime is built; share/opencc/ if vendored for Rime schemas"
echo "Copy profile example to your config/fcitx5/profile or set FCITX_TS_IM=pinyin."
echo "Next (PowerShell, Admin for 04+05):"
echo "  03-build-installer.ps1 -StageDir \"$STAGE\"   # 安装包 fcitx5-windows-setup.exe（与 04+05 等效于最终用户安装）"
echo "  04-deploy-to-portable.ps1 -Stage \"$STAGE\" -DeployDir C:\\Fcitx5Portable"
echo "  05-register-ime.ps1 -DeployDir C:\\Fcitx5Portable"
echo "  # or one shot: install-fcitx5-ime.ps1 -Stage \"$STAGE\" -DeployDir C:/Fcitx5Portable"
echo "Uninstall: uninstall-fcitx5-ime.ps1 -DeployDir \"...\" [-RemoveFiles] [-Force]"
