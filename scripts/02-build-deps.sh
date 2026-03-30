#!/usr/bin/env bash
# Step 2 — 依赖链：libime → fcitx5-chinese-addons → 可选 table-extra / rime / lua（同一 STAGE）。
# 默认先执行 fcitx5-windows 安装到 STAGE。若已用 PowerShell 跑过 01-build-fcitx5-windows.ps1 装入同一 STAGE，可 export SKIP_FCITX_WINDOWS=1 跳过本节。
# 默认 CMAKE_BUILD_TYPE 与 01-build-fcitx5-windows.ps1 一致为 Release；RelWithDebInfo 会令 libFcitx5Core / libfcitx5-x86_64 等体积显著变大。可 export CMAKE_BUILD_TYPE=RelWithDebInfo 覆盖。
# 依赖：MSYS2 CLANG64（或 MINGW64/UCRT64）、Ninja、Boost、libzstd、gettext；Rime 等见 docs/PINYIN_WINDOWS.md。
# 缺源码目录时会 git clone --depth 1（需 git）。可覆盖：LIBIME_GIT / LIBIME_TAG、CHINESE_ADDONS_GIT / CHINESE_ADDONS_TAG、
# RIME_TAG（默认 5.1.13）；可选仓库在 KenLM 子模块就绪后、[1/3] fcitx5-windows 之前统一克隆。
# librime：不再源码合并构建；从 MSYS2 安装包拷贝预编 librime-*.dll（无 Rime 内置 Lua，与 MSYS2 官方包一致）。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE="${STAGE:-$ROOT/stage}"
FCITX_BUILD="${FCITX_BUILD:-$ROOT/build-pinyin-fcitx}"
LIBIME_SRC="${LIBIME_SRC:-$ROOT/../libime}"
CHINESE_SRC="${CHINESE_SRC:-$ROOT/../fcitx5-chinese-addons}"
TABLE_EXTRA_SRC="${TABLE_EXTRA_SRC:-$ROOT/../fcitx5-table-extra}"
RIME_SRC="${RIME_SRC:-$ROOT/../fcitx5-rime}"
LUA_SRC="${LUA_SRC:-$ROOT/../fcitx5-lua}"
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
else
  LIBIME_BUILD="${LIBIME_BUILD:-$ROOT/build-pinyin-libime}"
  CHINESE_BUILD="${CHINESE_BUILD:-$ROOT/build-pinyin-chinese}"
  TABLE_EXTRA_BUILD="${TABLE_EXTRA_BUILD:-$ROOT/build-pinyin-table-extra}"
  RIME_BUILD="${RIME_BUILD:-$ROOT/build-pinyin-rime}"
  LUA_BUILD="${LUA_BUILD:-$ROOT/build-pinyin-lua}"
fi
BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 8)}"

# Clone into $1 if there is no CMakeLists.txt yet and $1 does not exist.
ensure_repo() {
  local dest="$1" url="$2" branch="${3:-}"
  if [[ -f "$dest/CMakeLists.txt" ]]; then
    return 0
  fi
  if [[ -e "$dest" ]]; then
    echo "error: path exists but is not a CMake project (no CMakeLists.txt): $dest" >&2
    exit 1
  fi
  if ! command -v git >/dev/null 2>&1; then
    echo "error: git not on PATH (cannot clone $url -> $dest)" >&2
    exit 1
  fi
  mkdir -p "$(dirname "$dest")"
  echo "==> git clone --depth 1 $url -> $dest"
  if [[ -n "$branch" ]]; then
    git clone --depth 1 --branch "$branch" "$url" "$dest"
  else
    git clone --depth 1 "$url" "$dest"
  fi
}

# Optional deps: clone only if $1 is entirely absent (do not overwrite partial trees).
# Clone failure (network, etc.) is non-fatal — the matching build block is skipped if CMakeLists.txt is missing.
maybe_clone_optional() {
  local dest="$1" url="$2" branch="${3:-}"
  if [[ -f "$dest/CMakeLists.txt" ]]; then
    return 0
  fi
  if [[ -e "$dest" ]]; then
    return 0
  fi
  if ! command -v git >/dev/null 2>&1; then
    echo "warning: git not on PATH; skip optional clone $url -> $dest" >&2
    return 0
  fi
  mkdir -p "$(dirname "$dest")"
  echo "==> git clone --depth 1 (optional) $url -> $dest"
  if [[ -n "$branch" ]]; then
    if ! git clone --depth 1 --branch "$branch" "$url" "$dest"; then
      echo "warning: optional clone failed: $url (network?) — continuing without this tree" >&2
      rm -rf "$dest" 2>/dev/null || true
    fi
  else
    if ! git clone --depth 1 "$url" "$dest"; then
      echo "warning: optional clone failed: $url (network?) — continuing without this tree" >&2
      rm -rf "$dest" 2>/dev/null || true
    fi
  fi
}

if [[ ! -f "$ROOT/CMakeLists.txt" ]]; then
  echo "ROOT invalid: $ROOT" >&2
  exit 1
fi

ensure_repo "$LIBIME_SRC" "${LIBIME_GIT:-https://github.com/fcitx/libime.git}" "${LIBIME_TAG:-}"
if [[ ! -f "$LIBIME_SRC/CMakeLists.txt" ]]; then
  echo "error: LIBIME_SRC invalid after clone: $LIBIME_SRC" >&2
  exit 1
fi

ensure_repo "$CHINESE_SRC" "${CHINESE_ADDONS_GIT:-https://github.com/fcitx/fcitx5-chinese-addons.git}" "${CHINESE_ADDONS_TAG:-}"
if [[ ! -f "$CHINESE_SRC/CMakeLists.txt" ]]; then
  echo "error: CHINESE_SRC invalid after clone: $CHINESE_SRC" >&2
  exit 1
fi

# Upstream fcitx5-chinese-addons: MSYS2 CLANG64 + libc++ (path→string) and scel2org5 (no sys/endian.h).
# Patch lives in this repo only; set CHINESE_ADDONS_SKIP_PATCH=1 to skip (e.g. upstream fixed or you use GCC only).
apply_fcitx5_chinese_addons_patch() {
  local pf="$ROOT/patches/fcitx5-chinese-addons-msys2-clang-libcxx.patch"
  [[ "${CHINESE_ADDONS_SKIP_PATCH:-0}" == "1" ]] && return 0
  [[ -f "$pf" ]] || return 0
  [[ $_is_msys -eq 1 ]] || return 0
  if git -C "$CHINESE_SRC" apply --check --whitespace=nowarn "$pf" 2>/dev/null; then
    echo "==> applying $(basename "$pf") to $CHINESE_SRC (set CHINESE_ADDONS_SKIP_PATCH=1 to skip)"
    git -C "$CHINESE_SRC" apply --whitespace=nowarn "$pf"
  fi
}

apply_fcitx5_chinese_addons_patch

# fcitx5-rime: C++20 + libc++ — path→string is explicit; fs::isdir takes std::string; RimeTraits paths are const char*.
apply_fcitx5_rime_patch() {
  local pf="$ROOT/patches/fcitx5-rime-windows-path-utf8.patch"
  [[ "${RIME_SKIP_PATCH:-0}" == "1" ]] && return 0
  [[ -f "$pf" ]] || return 0
  [[ -f "$RIME_SRC/CMakeLists.txt" ]] || return 0
  [[ $_is_msys -eq 1 ]] || return 0
  if git -C "$RIME_SRC" apply --check --whitespace=nowarn "$pf" 2>/dev/null; then
    echo "==> applying $(basename "$pf") to $RIME_SRC (set RIME_SKIP_PATCH=1 to skip)"
    git -C "$RIME_SRC" apply --whitespace=nowarn "$pf"
  fi
}

# libime vendors KenLM as a Git submodule (src/libime/core/kenlm).
if [[ ! -f "$LIBIME_SRC/src/libime/core/kenlm/lm/build_binary_main.cc" ]]; then
  echo "==> KenLM submodule missing under libime; running: git submodule update --init --recursive"
  (cd "$LIBIME_SRC" && git submodule update --init --recursive)
fi

# Clone optional trees before [1/3] fcitx5-windows.
echo "==> optional source trees (clone if absent; failures are non-fatal)"
maybe_clone_optional "$TABLE_EXTRA_SRC" "${TABLE_EXTRA_GIT:-https://github.com/fcitx/fcitx5-table-extra.git}" "${TABLE_EXTRA_TAG:-}"
maybe_clone_optional "$RIME_SRC" "${RIME_GIT:-https://github.com/fcitx/fcitx5-rime.git}" "${RIME_TAG:-5.1.13}"
maybe_clone_optional "$LUA_SRC" "${LUA_GIT:-https://github.com/fcitx/fcitx5-lua.git}" "${LUA_TAG:-}"

apply_fcitx5_rime_patch

# fcitx5-lua: CMake REGEX REPLACE leaves full llvm-objdump output when SONAME is absent (MinGW .dll.a);
# parse SONAME or (real.dll) inside import lib, else basename.
apply_fcitx5_lua_patch() {
  local pf
  [[ "${LUA_SKIP_PATCH:-0}" == "1" ]] && return 0
  [[ -f "$LUA_SRC/CMakeLists.txt" ]] || return 0
  [[ $_is_msys -eq 1 ]] || return 0
  # Keep the Windows build-compat patch, but disable local behavior/debug patches.
  for pf in \
    "$ROOT/patches/fcitx5-lua-mingw-objdump-resolve.patch" \
    "$ROOT/patches/fcitx5-lua-windows-no-newnamespace.patch" \
    "$ROOT/patches/fcitx5-lua-loader-trace.patch" \
    "$ROOT/patches/fcitx5-lua-loader-force-visible-fallback.patch" \
    "$ROOT/patches/fcitx5-lua-loader-deep-trace.patch" \
    "$ROOT/patches/fcitx5-lua-loader-minimal-core-register.patch" \
    "$ROOT/patches/fcitx5-lua-loader-bisect-core-groups.patch"; do
    [[ -f "$pf" ]] || continue
    if git -C "$LUA_SRC" apply --check --recount --ignore-whitespace --whitespace=nowarn "$pf" 2>/dev/null; then
      echo "==> applying $(basename "$pf") to $LUA_SRC (set LUA_SKIP_PATCH=1 to skip)"
      git -C "$LUA_SRC" apply --recount --ignore-whitespace --whitespace=nowarn "$pf"
    fi
  done
}

apply_fcitx5_lua_patch

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

# LibIME*Config.cmake IMPORTED_LOCATION points at .../bin/libime_pinyindict (no .exe). MinGW only
# ships *.exe; Ninja needs the extensionless path. Use cp (not cmake -E copy): on Windows, CMake
# may normalize/copy extensionless destinations incorrectly and leave the Ninja dependency missing.
copy_tool_no_ext() {
  local src="$1" dest="$2" win_cmd wsrc wdest
  if [[ ! -f "$src" ]]; then
    echo "error: copy_tool_no_ext: source not a file: $src" >&2
    return 1
  fi
  mkdir -p "$(dirname "$dest")"
  # MSYS cp treats foo and foo.exe as one file; Ninja needs a real path without .exe for IMPORTED deps.
  case "$(uname -s 2>/dev/null)" in
    MSYS_NT* | MINGW*)
      wsrc=$(cygpath -w "$src")
      # cygpath -w on extensionless paths often resolves to the same *.exe path as $src, and cmd then
      # errors: "The file cannot be copied onto itself." Build the Windows dest from dir + basename only.
      wdestdir=$(cygpath -w "$(dirname "$dest")")
      wdest="${wdestdir}\\$(basename "$dest")"
      if [[ -n "${SYSTEMROOT:-}" ]]; then
        win_cmd="$(cygpath -u "$SYSTEMROOT")/System32/cmd.exe"
      else
        win_cmd="/c/Windows/System32/cmd.exe"
      fi
      [[ -f "$win_cmd" ]] || win_cmd="/c/Windows/System32/cmd.exe"
      # MSYS2_ARG_CONV_EXCL: stop rewriting "copy" / paths so cmd gets a plain `copy /Y src dest`.
      if ! env MSYS2_ARG_CONV_EXCL='*' "$win_cmd" /c copy /Y "$wsrc" "$wdest" </dev/null; then
        echo "error: Windows copy failed ($src -> $dest); cmd=$win_cmd" >&2
        return 1
      fi
      if [[ ! -f "$dest" ]]; then
        echo "error: expected file missing after copy: $dest" >&2
        return 1
      fi
      ;;
    *)
      cp -f "$src" "$dest"
      ;;
  esac
}

ensure_libime_tool_names() {
  echo "==> ensuring libime CLI tools (extensionless names for fcitx5-chinese-addons / Ninja)"
  local _tool
  for _tool in libime_pinyindict libime_tabledict libime_slm_build_binary libime_prediction libime_history libime_migrate_fcitx4_pinyin libime_migrate_fcitx4_table; do
    if [[ -f "$LIBIME_BUILD/bin/${_tool}.exe" ]] && [[ ! -f "$STAGE/bin/${_tool}.exe" ]]; then
      mkdir -p "$STAGE/bin"
      copy_tool_no_ext "$LIBIME_BUILD/bin/${_tool}.exe" "$STAGE/bin/${_tool}.exe"
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
    if [[ -f "$STAGE/bin/${_tool}" ]]; then
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

# Rime 依赖 DLL（不含 librime 本体 — 本体由 MSYS2 预编包拷贝）。
# CLANG64 上为 libglog-2.dll（不是 libglog.dll）；libglog 还依赖 libgflags、libunwind。
copy_librime_dependency_dlls() {
  local _bin
  _bin="$(mingw_dll_source_bin)"
  [[ -n "$_bin" ]] || return 0
  mkdir -p "$STAGE/bin"
  local _f
  shopt -s nullglob
  for _f in "$_bin"/libopencc*.dll "$_bin"/libyaml-cpp.dll \
    "$_bin"/libleveldb.dll "$_bin"/libglog*.dll "$_bin"/libmarisa*.dll \
    "$_bin"/libgflags*.dll "$_bin"/libunwind.dll \
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

verify_librime_prebuilt_in_stage() {
  shopt -s nullglob
  local _lr=( "$STAGE/bin"/librime-*.dll )
  shopt -u nullglob
  if [[ ${#_lr[@]} -eq 0 ]]; then
    echo "error: no librime-*.dll under $STAGE/bin (MSYS2 prebuilt)." >&2
    echo "  CLANG64: pacman -S mingw-w64-clang-x86_64-librime mingw-w64-clang-x86_64-librime-data" >&2
    echo "  MINGW64: pacman -S mingw-w64-x86_64-librime mingw-w64-x86_64-librime-data" >&2
    exit 1
  fi
  echo "==> [5a] librime: MSYS2 prebuilt $(basename "${_lr[0]}") + deps (no merged librime-lua in DLL)"
}

if [[ -f "$RIME_SRC/CMakeLists.txt" ]]; then
  echo "==> [5] fcitx5-rime"
  if ! vendor_rime_share_into_stage; then
    echo "error: rime-data missing under stage and not found under /clang64 or /mingw64." >&2
    echo "  Install: pacman -S mingw-w64-clang-x86_64-librime-data (CLANG64; replaces old rime-data)" >&2
    exit 1
  fi
  copy_librime_runtime_dlls
  if [[ $_is_msys -eq 1 ]]; then
    verify_librime_prebuilt_in_stage
  else
    echo "==> [5a] librime: non-MSYS — copy skipped MSYS bin paths; ensure fcitx5-rime links and runtime can find librime" >&2
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
  if [[ $_is_msys -eq 1 ]] && command -v pkg-config >/dev/null 2>&1 && ! pkg-config --exists lua 2>/dev/null; then
    echo "warning: pkg-config module 'lua' not found; skip fcitx5-lua (optional addon)." >&2
    echo "  MSYS2 CLANG64: pacman -S mingw-w64-clang-x86_64-lua" >&2
  else
    rm -rf "$LUA_BUILD"
    cmake -S "$LUA_SRC" -B "$LUA_BUILD" -G Ninja \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DCMAKE_INSTALL_PREFIX="$STAGE" \
      -DCMAKE_PREFIX_PATH="$STAGE" \
      -DENABLE_TEST=OFF
    cmake --build "$LUA_BUILD" -j"$JOBS"
    cmake --install "$LUA_BUILD" --prefix "$STAGE"
    copy_lua_runtime_dlls
  fi
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
echo "  bin/     — Fcitx5.exe, *.dll, fcitx5-x86_64.dll (if built), librime (MSYS2 prebuilt) + rime deps, lua if built"
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
