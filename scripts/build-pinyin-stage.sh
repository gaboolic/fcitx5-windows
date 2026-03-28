#!/usr/bin/env bash
# 可重复：构建 fcitx5-windows → install 到 prefix → libime → chinese-addons →
# 可选 fcitx5-table-extra、fcitx5-rime、fcitx5-lua（同一 prefix）。
# 依赖：MSYS2 Clang64、Ninja、Boost、libzstd、gettext；Rime：从源码合并编 librime+librime-lua（同 fcitx5-prebuilder / macOS）或回退为拷贝 MSYS2 预编译 librime；rime-data；Lua：lua.pc；见 docs/PINYIN_WINDOWS.md。
# 详见 docs/PINYIN_WINDOWS.md
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE="${STAGE:-$ROOT/stage-pinyin}"
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

# DefaultLanguageModelResolver uses LIBIME_INSTALL_LIBDATADIR (CMake-time absolute path) when
# LIBIME_MODEL_DIRS is unset. CI stage paths (e.g. D:/a/...) do not exist on end-user PCs →
# zh_CN.lm never loads → decoder scores are wrong ("xiexieni" → garbage). TSF sets
# LIBIME_MODEL_DIRS at runtime; upstream splits that env only by ':' which breaks 'C:\...'.
patch_libime_languagemodel_dirs_for_win32() {
  [[ $_is_msys -eq 1 ]] || return 0
  local f="$LIBIME_SRC/src/libime/core/languagemodel.cpp"
  [[ -f "$f" ]] || return 0
  if grep -q 'split(modelDirs, ";")' "$f" 2>/dev/null; then
    return 0
  fi
  echo "==> patch libime languagemodel.cpp: LIBIME_MODEL_DIRS split ';' on _WIN32"
  perl -i.bak -0777 -pe '
    s/if \(modelDirs && modelDirs\[0\]\) \{\R\s*dirs = fcitx::stringutils::split\(modelDirs, ":"\);/
if (modelDirs \&\& modelDirs[0]) {\n#ifdef _WIN32\n        dirs = fcitx::stringutils::split(modelDirs, ";");\n#else\n        dirs = fcitx::stringutils::split(modelDirs, ":");\n#endif/s;
  ' "$f"
  rm -f "${f}.bak"
  if ! grep -q 'split(modelDirs, ";")' "$f" 2>/dev/null; then
    echo "error: failed to patch libime languagemodel.cpp (LIBIME_MODEL_DIRS / _WIN32)" >&2
    exit 1
  fi
}
patch_libime_languagemodel_dirs_for_win32

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

# LibIME *Config.cmake sets IMPORTED_LOCATION to .../bin/libime_pinyindict (no .exe). MinGW only
# installs libime_pinyindict.exe. Native Windows Ninja then looks for the extensionless path and
# fails ("missing and no known rule to make it") even if MSYS created a copy — fix the *installed*
# CMake package so find_package points at the real .exe names.
patch_libime_imported_locations_for_mingw() {
  [[ $_is_msys -eq 1 ]] || return 0
  echo "==> patch LibIME *Config.cmake IMPORTED_LOCATION -> *.exe (MinGW / Ninja)"
  local f
  shopt -s nullglob
  for f in "$STAGE"/lib/cmake/LibIME*/LibIME*Config.cmake; do
    [[ -f "$f" ]] || continue
    if command -v perl >/dev/null 2>&1; then
      perl -i.bak -pe '
        next unless /IMPORTED_LOCATION/;
        s/libime_slm_build_binary(?!\.exe)"/libime_slm_build_binary.exe"/g;
        s/libime_prediction(?!\.exe)"/libime_prediction.exe"/g;
        s/libime_history(?!\.exe)"/libime_history.exe"/g;
        s/libime_pinyindict(?!\.exe)"/libime_pinyindict.exe"/g;
        s/libime_tabledict(?!\.exe)"/libime_tabledict.exe"/g;
        s/libime_migrate_fcitx4_pinyin(?!\.exe)"/libime_migrate_fcitx4_pinyin.exe"/g;
        s/libime_migrate_fcitx4_table(?!\.exe)"/libime_migrate_fcitx4_table.exe"/g;
      ' "$f"
      rm -f "${f}.bak"
    else
      # No perl: sed only touches IMPORTED_LOCATION lines (msys perl package: pacman -S perl).
      local _t
      for _t in libime_slm_build_binary libime_prediction libime_history libime_pinyindict libime_tabledict libime_migrate_fcitx4_pinyin libime_migrate_fcitx4_table; do
        sed -i.bak "/IMPORTED_LOCATION/ s|/${_t}\"|/${_t}.exe\"|g" "$f"
      done
      rm -f "${f}.bak"
    fi
  done
  shopt -u nullglob
}
patch_libime_imported_locations_for_mingw

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

# Upstream master may assume libstdc++ (implicit path→string) and BSD sys/endian.h; Clang/libc++ on
# MinGW needs explicit path.string() and a _WIN32 branch in scel2org5.cpp.
patch_fcitx5_chinese_addons_for_mingw() {
  [[ $_is_msys -eq 1 ]] || return 0
  local pinyin_cpp="$CHINESE_SRC/im/pinyin/pinyin.cpp"
  local scel="$CHINESE_SRC/tools/scel2org5.cpp"
  echo "==> patch fcitx5-chinese-addons for MinGW (pinyin path.string, scel2org5 endian)"
  if [[ -f "$pinyin_cpp" ]] && ! grep -q 'loadDict(file.string(), persistentTask_)' "$pinyin_cpp"; then
    if grep -q 'loadDict(file, persistentTask_)' "$pinyin_cpp"; then
      sed -i.bak \
        -e 's/loadDict(file, persistentTask_)/loadDict(file.string(), persistentTask_)/g' \
        -e 's/loadDict(file.second, tasks_)/loadDict(file.second.string(), tasks_)/g' \
        "$pinyin_cpp"
      rm -f "${pinyin_cpp}.bak"
    fi
  fi
  # In s/// replacement, \R is not a newline (it became literal "R" in CI). Use \n in replacement;
  # keep \R only in the pattern to match LF or CRLF. Also repair one-line corruption from the old bug.
  if [[ -f "$scel" ]] && command -v perl >/dev/null 2>&1; then
    perl -i.bak -0777 -pe '
      s/#elif defined\(_WIN32\)R#include <stdint\.h>R#define le16toh\(x\) \(x\)R#define le32toh\(x\) \(x\)R#elseR#include <sys\/endian\.h>/#elif defined(_WIN32)\n#include <stdint.h>\n#define le16toh(x) (x)\n#define le32toh(x) (x)\n#else\n#include <sys\/endian.h>/gs;
      unless (m/#elif defined\(_WIN32\)\s*\n\s*#include <stdint\.h>/) {
        s/#else\R(#include <sys\/endian\.h>)/#elif defined(_WIN32)\n#include <stdint.h>\n#define le16toh(x) (x)\n#define le32toh(x) (x)\n#else\n$1/gs;
      }
    ' "$scel"
    rm -f "${scel}.bak"
  elif [[ -f "$scel" ]]; then
    echo "warning: perl missing; scel2org5.cpp may fail on Windows (pacman -S perl)" >&2
  fi
}
patch_fcitx5_chinese_addons_for_mingw

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

build_merged_librime_with_lua() {
  local _prefix
  _prefix="$(mingw_toolchain_prefix)"
  [[ -n "$_prefix" ]] || return 1
  echo "==> [5a] librime + librime-lua (merged into librime DLL, same idea as fcitx5-prebuilder / fcitx5-macos deps)"
  prepare_librime_lua_plugin_dir || return 1
  rm -rf "$LIBRIME_BUILD"
  # 与 MSYS2 mingw-w64-librime PKGBUILD 对齐的 glog 宏，避免与 Clang64 的 libglog DLL 不匹配。
  cmake -S "$LIBRIME_SRC" -B "$LIBRIME_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$STAGE" \
    -DCMAKE_PREFIX_PATH="${_prefix};${STAGE}" \
    -DBUILD_TEST=OFF \
    -DBUILD_MERGED_PLUGINS=ON \
    -DCMAKE_DLL_NAME_WITH_SOVERSION=ON \
    "-DCMAKE_CXX_FLAGS=-DNDEBUG -DGLOG_USE_GLOG_EXPORT -DGLOG_USE_GFLAGS" \
    || return 1
  cmake --build "$LIBRIME_BUILD" -j"$JOBS" || return 1
  cmake --install "$LIBRIME_BUILD" --prefix "$STAGE" || return 1
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
    if [[ $_is_msys -ne 1 ]]; then
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
echo "TSF install (Admin PowerShell):"
echo "  pwsh -File scripts/install-fcitx5-ime.ps1 -Stage \"$STAGE\" -DeployDir \"C:/Fcitx5Portable\""
echo "Uninstall: scripts/uninstall-fcitx5-ime.ps1 -DeployDir \"...\" [-RemoveFiles] [-Force]"
echo "Or: deploy-ime-stage.ps1 / .sh (same as before; -Unregister = uninstall)"
