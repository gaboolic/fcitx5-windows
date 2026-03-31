#!/usr/bin/env bash
# Step 2 — 依赖链：libime → 可选 lua → fcitx5-chinese-addons → 可选 table-extra / rime（同一 STAGE）。
# 默认先执行 fcitx5-windows 安装到 STAGE。若已用 PowerShell 跑过 01-build-fcitx5-windows.ps1 装入同一 STAGE，可 export SKIP_FCITX_WINDOWS=1 跳过本节。
# 默认 CMAKE_BUILD_TYPE 与 01-build-fcitx5-windows.ps1 一致为 Release；RelWithDebInfo 会令 libFcitx5Core / libfcitx5-x86_64 等体积显著变大。可 export CMAKE_BUILD_TYPE=RelWithDebInfo 覆盖。
# 依赖：MSYS2 CLANG64（或 MINGW64/UCRT64）、Ninja、Boost、libzstd、gettext；Rime 等见 docs/PINYIN_WINDOWS.md。
# 缺源码目录时会 git clone --depth 1（需 git）。可覆盖：LIBIME_GIT / LIBIME_TAG、CHINESE_ADDONS_GIT / CHINESE_ADDONS_TAG、
# RIME_TAG（默认 5.1.13）；可选仓库在 KenLM 子模块就绪后、[1/3] fcitx5-windows 之前统一克隆。
# librime 运行时按 weasel CI 逻辑处理：下载 librime GitHub Release 的 Windows-clang-x64 主包与 deps 包，
# 直接取 dist/lib/rime.dll、rime.lib、头文件，并拷贝 share/opencc；不走本地 librime 源码构建。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE="${STAGE:-$ROOT/stage}"
FCITX_BUILD="${FCITX_BUILD:-$ROOT/build-pinyin-fcitx}"
LIBIME_SRC="${LIBIME_SRC:-$ROOT/../libime}"
LIBRIME_SRC="${LIBRIME_SRC:-$ROOT/../librime}"
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
  LIBRIME_BUILD="${LIBRIME_BUILD:-/tmp/fcitx5-pinyin-librime}"
  CHINESE_BUILD="${CHINESE_BUILD:-/tmp/fcitx5-pinyin-chinese}"
  TABLE_EXTRA_BUILD="${TABLE_EXTRA_BUILD:-/tmp/fcitx5-pinyin-table-extra}"
  RIME_BUILD="${RIME_BUILD:-/tmp/fcitx5-pinyin-rime}"
  LUA_BUILD="${LUA_BUILD:-/tmp/fcitx5-pinyin-lua}"
else
  LIBIME_BUILD="${LIBIME_BUILD:-$ROOT/build-pinyin-libime}"
  LIBRIME_BUILD="${LIBRIME_BUILD:-$ROOT/build-pinyin-librime}"
  CHINESE_BUILD="${CHINESE_BUILD:-$ROOT/build-pinyin-chinese}"
  TABLE_EXTRA_BUILD="${TABLE_EXTRA_BUILD:-$ROOT/build-pinyin-table-extra}"
  RIME_BUILD="${RIME_BUILD:-$ROOT/build-pinyin-rime}"
  LUA_BUILD="${LUA_BUILD:-$ROOT/build-pinyin-lua}"
fi
BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 8)}"
OPENCC_PKG_FILE="${OPENCC_PKG_FILE:-}"
LIBRIME_PKG_FILE="${LIBRIME_PKG_FILE:-}"
RIME_RUNTIME_ROOT="${RIME_RUNTIME_ROOT:-/tmp/rime-runtime}"
RIME_RUNTIME_DOWNLOAD="${RIME_RUNTIME_DOWNLOAD:-1}"
MSYS2_MIRROR_ROOT="${MSYS2_MIRROR_ROOT:-https://mirror.msys2.org/mingw}"
OPENCC_DOWNLOAD_VERSION="${OPENCC_DOWNLOAD_VERSION:-}"
LIBRIME_DOWNLOAD_VERSION="${LIBRIME_DOWNLOAD_VERSION:-}"
LIBRIME_RELEASE_TAG="${LIBRIME_RELEASE_TAG:-latest}"
LIBRIME_RELEASE_PKG_FILE="${LIBRIME_RELEASE_PKG_FILE:-}"
LIBRIME_RELEASE_DEPS_PKG_FILE="${LIBRIME_RELEASE_DEPS_PKG_FILE:-}"
LIBRIME_RELEASE_ROOT="${LIBRIME_RELEASE_ROOT:-$RIME_RUNTIME_ROOT/librime-release}"
_RIME_RUNTIME_PREPARED=0
_RIME_RUNTIME_PREFIX=""
_LIBRIME_RELEASE_PREPARED=0
_LIBRIME_RELEASE_MAIN_DIR=""
_LIBRIME_RELEASE_DEPS_DIR=""
_LIBRIME_RELEASE_VERSION=""

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
    "$ROOT/patches/fcitx5-lua-loader-min-anchor-trace.patch"; do
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

mingw_dll_source_bin() {
  if [[ -d /clang64/bin ]]; then
    echo /clang64/bin
  elif [[ -d /mingw64/bin ]]; then
    echo /mingw64/bin
  else
    echo ""
  fi
}

mingw_prefix_dir() {
  if [[ -d /clang64 ]]; then
    echo /clang64
  elif [[ -d /mingw64 ]]; then
    echo /mingw64
  elif [[ -d /ucrt64 ]]; then
    echo /ucrt64
  elif [[ -d /clangarm64 ]]; then
    echo /clangarm64
  else
    echo ""
  fi
}

mingw_repo_name() {
  if [[ -d /clang64 ]]; then
    echo clang64
  elif [[ -d /mingw64 ]]; then
    echo mingw64
  elif [[ -d /ucrt64 ]]; then
    echo ucrt64
  elif [[ -d /clangarm64 ]]; then
    echo clangarm64
  else
    echo ""
  fi
}

mingw_package_prefix() {
  if [[ -d /clang64 ]]; then
    echo mingw-w64-clang-x86_64
  elif [[ -d /mingw64 ]]; then
    echo mingw-w64-x86_64
  elif [[ -d /ucrt64 ]]; then
    echo mingw-w64-ucrt-x86_64
  elif [[ -d /clangarm64 ]]; then
    echo mingw-w64-clang-aarch64
  else
    echo ""
  fi
}

native_path() {
  local p="$1"
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -m "$p"
  else
    printf '%s\n' "$p"
  fi
}

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

# --- Optional: fcitx5-lua (build before chinese-addons so pinyin.lua is installed) ---
if [[ -f "$LUA_SRC/CMakeLists.txt" ]]; then
  echo "==> [3] fcitx5-lua"
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

echo "==> [4] fcitx5-chinese-addons"
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

normalize_windows_libime_table_paths() {
  local _inputmethod_dir="$STAGE/share/fcitx5/inputmethod"
  local _conf
  [[ -d "$_inputmethod_dir" ]] || return 0
  # These configs point at libime-installed dictionaries under share/libime.
  # On Windows, the generated File= entry may contain the absolute build-stage
  # path, which breaks installed packages on other machines. Re-anchor them
  # relative to share/fcitx5/inputmethod/.
  shopt -s nullglob
  for _conf in \
    "$_inputmethod_dir"/cangjie.conf \
    "$_inputmethod_dir"/db.conf \
    "$_inputmethod_dir"/erbi.conf \
    "$_inputmethod_dir"/qxm.conf \
    "$_inputmethod_dir"/wanfeng.conf \
    "$_inputmethod_dir"/wbpy.conf \
    "$_inputmethod_dir"/wbx.conf \
    "$_inputmethod_dir"/zrm.conf; do
    [[ -f "$_conf" ]] || continue
    sed -E -i 's#^File=.*/([^/]+\.main\.dict)$#File=../libime/\1#' "$_conf"
  done
  shopt -u nullglob
}

normalize_windows_libime_table_paths

# --- Optional: fcitx5-table-extra (extra table IMs / dicts; needs LibIMETable from stage) ---
if [[ -f "$TABLE_EXTRA_SRC/CMakeLists.txt" ]]; then
  echo "==> [5] fcitx5-table-extra"
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

download_runtime_package() {
  local url="$1" dest="$2"
  mkdir -p "$(dirname "$dest")"
  if [[ -f "$dest" ]]; then
    return 0
  fi
  echo "==> downloading $(basename "$dest")"
  if command -v curl >/dev/null 2>&1; then
    curl -L --fail --retry 3 --output "$dest" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$dest" "$url"
  elif command -v powershell.exe >/dev/null 2>&1; then
    powershell.exe -NoLogo -NoProfile -Command \
      "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -UseBasicParsing -Uri '$url' -OutFile '$dest'"
  else
    echo "error: no downloader available (need curl, wget, or powershell.exe) for $url" >&2
    exit 1
  fi
}

resolve_librime_release_assets() {
  local _tag="${LIBRIME_RELEASE_TAG:-latest}" _pattern_main _pattern_deps _ps _out
  case "$(mingw_prefix_dir)" in
    /clang64)
      _pattern_main='^rime-[0-9A-Fa-f]+-Windows-clang-x64\.7z$'
      _pattern_deps='^rime-deps-[0-9A-Fa-f]+-Windows-clang-x64\.7z$'
      ;;
    *)
      echo "error: librime release download is currently configured only for CLANG64." >&2
      exit 1
      ;;
  esac
  _ps=$(cat <<EOF
\$ErrorActionPreference = 'Stop'
\$tag = '${_tag}'
\$api = if (\$tag -eq 'latest') {
  'https://api.github.com/repos/rime/librime/releases/latest'
} else {
  'https://api.github.com/repos/rime/librime/releases/tags/' + \$tag
}
\$resp = Invoke-RestMethod -UseBasicParsing -Headers @{ Accept = 'application/vnd.github.v3+json' } -Uri \$api
\$main = \$resp.assets | Where-Object { \$_.name -match '${_pattern_main}' } | Select-Object -First 1
\$deps = \$resp.assets | Where-Object { \$_.name -match '${_pattern_deps}' } | Select-Object -First 1
if (-not \$main) { throw 'missing main Windows-clang-x64 asset in librime release' }
if (-not \$deps) { throw 'missing deps Windows-clang-x64 asset in librime release' }
Write-Output \$resp.tag_name
Write-Output \$main.browser_download_url
Write-Output \$deps.browser_download_url
EOF
)
  _out="$(powershell.exe -NoLogo -NoProfile -Command "$_ps" | tr -d '\r')"
  [[ -n "$_out" ]] || {
    echo "error: failed to resolve librime release asset URLs." >&2
    exit 1
  }
  printf '%s\n' "$_out"
}

ensure_librime_release_packages_downloaded() {
  local _resolved _tag _main_url _deps_url
  if [[ -n "$LIBRIME_RELEASE_PKG_FILE" && -n "$LIBRIME_RELEASE_DEPS_PKG_FILE" ]]; then
    return 0
  fi
  mapfile -t _resolved < <(resolve_librime_release_assets)
  [[ ${#_resolved[@]} -ge 3 ]] || {
    echo "error: incomplete librime release metadata." >&2
    exit 1
  }
  _tag="${_resolved[0]}"
  _LIBRIME_RELEASE_VERSION="$_tag"
  _main_url="${_resolved[1]}"
  _deps_url="${_resolved[2]}"
  if [[ -z "$LIBRIME_RELEASE_PKG_FILE" ]]; then
    LIBRIME_RELEASE_PKG_FILE="$RIME_RUNTIME_ROOT/releases/${_tag}/$(basename "$_main_url")"
    download_runtime_package "$_main_url" "$LIBRIME_RELEASE_PKG_FILE"
  fi
  if [[ -z "$LIBRIME_RELEASE_DEPS_PKG_FILE" ]]; then
    LIBRIME_RELEASE_DEPS_PKG_FILE="$RIME_RUNTIME_ROOT/releases/${_tag}/$(basename "$_deps_url")"
    download_runtime_package "$_deps_url" "$LIBRIME_RELEASE_DEPS_PKG_FILE"
  fi
}

extract_runtime_package() {
  local pkg="$1" dest="$2"
  [[ -f "$pkg" ]] || {
    echo "error: runtime package file not found: $pkg" >&2
    exit 1
  }
  mkdir -p "$dest"
  if command -v bsdtar >/dev/null 2>&1; then
    bsdtar -xf "$pkg" -C "$dest"
  else
    tar -xf "$pkg" -C "$dest"
  fi
}

msys2_installed_pkg_version() {
  local pkg="$1"
  command -v pacman >/dev/null 2>&1 || return 1
  pacman -Q "$pkg" 2>/dev/null | awk 'NR==1 { print $2 }'
}

ensure_matching_runtime_packages_downloaded() {
  local required_opencc_dll="$1"
  local repo pkg_prefix opencc_version librime_version opencc_url librime_url
  repo="$(mingw_repo_name)"
  pkg_prefix="$(mingw_package_prefix)"
  [[ -n "$repo" && -n "$pkg_prefix" ]] || {
    echo "error: unable to determine MSYS2 repo/package prefix for runtime download." >&2
    exit 1
  }
  if [[ -z "$OPENCC_PKG_FILE" ]]; then
    opencc_version="$OPENCC_DOWNLOAD_VERSION"
    if [[ -z "$opencc_version" ]]; then
      case "$required_opencc_dll" in
        libopencc-1.1.dll) opencc_version="1.1.9-1" ;;
        libopencc-1.2.dll) opencc_version="$(msys2_installed_pkg_version "${pkg_prefix}-opencc" || true)" ;;
      esac
    fi
    [[ -n "$opencc_version" ]] || {
      echo "error: unable to infer OpenCC package version for $required_opencc_dll." >&2
      echo "  Set OPENCC_PKG_FILE or OPENCC_DOWNLOAD_VERSION explicitly." >&2
      exit 1
    }
    OPENCC_PKG_FILE="$RIME_RUNTIME_ROOT/packages/${pkg_prefix}-opencc-${opencc_version}-any.pkg.tar.zst"
    opencc_url="$MSYS2_MIRROR_ROOT/$repo/$(basename "$OPENCC_PKG_FILE")"
    download_runtime_package "$opencc_url" "$OPENCC_PKG_FILE"
  fi
  if [[ -z "$LIBRIME_PKG_FILE" ]]; then
    librime_version="$LIBRIME_DOWNLOAD_VERSION"
    if [[ -z "$librime_version" ]]; then
      librime_version="$(msys2_installed_pkg_version "${pkg_prefix}-librime" || true)"
    fi
    [[ -n "$librime_version" ]] || {
      echo "error: unable to infer librime package version." >&2
      echo "  Set LIBRIME_PKG_FILE or LIBRIME_DOWNLOAD_VERSION explicitly." >&2
      exit 1
    }
    LIBRIME_PKG_FILE="$RIME_RUNTIME_ROOT/packages/${pkg_prefix}-librime-${librime_version}-any.pkg.tar.zst"
    librime_url="$MSYS2_MIRROR_ROOT/$repo/$(basename "$LIBRIME_PKG_FILE")"
    download_runtime_package "$librime_url" "$LIBRIME_PKG_FILE"
  fi
}

prepare_rime_runtime_prefix() {
  local _prefix _required_opencc
  _prefix="$(mingw_prefix_dir)"
  [[ -n "$_prefix" ]] || return 1
  if [[ $_RIME_RUNTIME_PREPARED -eq 1 ]]; then
    printf '%s\n' "$_RIME_RUNTIME_PREFIX"
    return 0
  fi
  if [[ -d "$RIME_RUNTIME_ROOT$_prefix/bin" ]]; then
    _RIME_RUNTIME_PREFIX="$RIME_RUNTIME_ROOT$_prefix"
    _RIME_RUNTIME_PREPARED=1
    printf '%s\n' "$_RIME_RUNTIME_PREFIX"
    return 0
  fi
  if [[ -z "$OPENCC_PKG_FILE" && -z "$LIBRIME_PKG_FILE" ]]; then
    _required_opencc="$(detect_librime_opencc_import_name_from_dir "$_prefix/bin" || true)"
    if [[ -z "$_required_opencc" || -f "$_prefix/bin/$_required_opencc" ]]; then
      _RIME_RUNTIME_PREFIX="$_prefix"
      _RIME_RUNTIME_PREPARED=1
      printf '%s\n' "$_RIME_RUNTIME_PREFIX"
      return 0
    fi
    if [[ "$RIME_RUNTIME_DOWNLOAD" != "1" ]]; then
      echo "error: librime runtime expects $_required_opencc but it is missing under $_prefix/bin" >&2
      echo "  Set OPENCC_PKG_FILE/LIBRIME_PKG_FILE, pre-extract RIME_RUNTIME_ROOT, or set RIME_RUNTIME_DOWNLOAD=1." >&2
      exit 1
    fi
    echo "==> missing $_required_opencc under $_prefix/bin; downloading matching MSYS2 runtime packages"
    ensure_matching_runtime_packages_downloaded "$_required_opencc"
  fi
  mkdir -p "$RIME_RUNTIME_ROOT"
  rm -rf "$RIME_RUNTIME_ROOT$_prefix"
  if [[ -n "$OPENCC_PKG_FILE" ]]; then
    echo "==> extracting OpenCC runtime package: $OPENCC_PKG_FILE"
    extract_runtime_package "$OPENCC_PKG_FILE" "$RIME_RUNTIME_ROOT"
  fi
  if [[ -n "$LIBRIME_PKG_FILE" ]]; then
    echo "==> extracting librime runtime package: $LIBRIME_PKG_FILE"
    extract_runtime_package "$LIBRIME_PKG_FILE" "$RIME_RUNTIME_ROOT"
  fi
  _RIME_RUNTIME_PREFIX="$RIME_RUNTIME_ROOT$_prefix"
  if [[ ! -d "$_RIME_RUNTIME_PREFIX" ]]; then
    echo "error: extracted runtime prefix missing: $_RIME_RUNTIME_PREFIX" >&2
    exit 1
  fi
  _RIME_RUNTIME_PREPARED=1
  printf '%s\n' "$_RIME_RUNTIME_PREFIX"
}

prepare_librime_release_root() {
  if [[ $_LIBRIME_RELEASE_PREPARED -eq 1 ]]; then
    return 0
  fi
  ensure_librime_release_packages_downloaded
  mkdir -p "$LIBRIME_RELEASE_ROOT"
  _LIBRIME_RELEASE_MAIN_DIR="$LIBRIME_RELEASE_ROOT/main"
  _LIBRIME_RELEASE_DEPS_DIR="$LIBRIME_RELEASE_ROOT/deps"
  rm -rf "$_LIBRIME_RELEASE_MAIN_DIR" "$_LIBRIME_RELEASE_DEPS_DIR"
  mkdir -p "$_LIBRIME_RELEASE_MAIN_DIR" "$_LIBRIME_RELEASE_DEPS_DIR"
  echo "==> extracting librime release package: $LIBRIME_RELEASE_PKG_FILE" >&2
  extract_runtime_package "$LIBRIME_RELEASE_PKG_FILE" "$_LIBRIME_RELEASE_MAIN_DIR"
  echo "==> extracting librime release deps package: $LIBRIME_RELEASE_DEPS_PKG_FILE" >&2
  extract_runtime_package "$LIBRIME_RELEASE_DEPS_PKG_FILE" "$_LIBRIME_RELEASE_DEPS_DIR"
  _LIBRIME_RELEASE_PREPARED=1
}

find_llvm_readobj() {
  local _tool
  for _tool in \
    /clang64/bin/llvm-readobj.exe \
    /mingw64/bin/llvm-readobj.exe \
    /ucrt64/bin/llvm-readobj.exe \
    /clangarm64/bin/llvm-readobj.exe; do
    [[ -x "$_tool" ]] && {
      echo "$_tool"
      return 0
    }
  done
  command -v llvm-readobj 2>/dev/null || true
}

detect_librime_opencc_import_name_from_dir() {
  local _dir="$1" _tool
  shopt -s nullglob
  local _lr=( "$_dir"/librime-*.dll )
  shopt -u nullglob
  [[ ${#_lr[@]} -gt 0 ]] || return 1
  _tool="$(find_llvm_readobj)"
  [[ -n "$_tool" ]] || return 1
  "$_tool" --coff-imports "${_lr[0]}" 2>/dev/null |
    sed -n 's/^[[:space:]]*Name: \(libopencc-[^[:space:]]*\.dll\)$/\1/p' |
    sed -n '1p'
}

vendor_rime_share_into_stage() {
  local rime_data="$STAGE/share/rime-data"
  if [[ -d "$rime_data" ]] && [[ -n "$(ls -A "$rime_data" 2>/dev/null)" ]]; then
    return 0
  fi
  local src _runtime_prefix
  _runtime_prefix="$(prepare_rime_runtime_prefix || true)"
  for src in \
    "${_runtime_prefix:+$_runtime_prefix/share/rime-data}" \
    /clang64/share/rime-data /mingw64/share/rime-data; do
    [[ -n "$src" ]] || continue
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
  local _bin _runtime_prefix _runtime_bin _opencc_required _system_prefix
  _bin="$(mingw_dll_source_bin)"
  [[ -n "$_bin" ]] || return 0
  _runtime_prefix="$(prepare_rime_runtime_prefix)"
  _runtime_bin="$_runtime_prefix/bin"
  _system_prefix="$(mingw_prefix_dir)"
  mkdir -p "$STAGE/bin"
  local _f
  shopt -s nullglob
  if [[ "$_runtime_prefix" != "$_system_prefix" ]]; then
    rm -f "$STAGE/bin"/libopencc*.dll
  fi
  _opencc_required="$(detect_librime_opencc_import_name_from_dir "$_runtime_bin" || true)"
  for _f in "$_runtime_bin"/libopencc*.dll; do
    [[ -f "$_f" ]] && cp -f "$_f" "$STAGE/bin/"
  done
  if [[ -n "$_opencc_required" && ! -f "$STAGE/bin/$_opencc_required" ]]; then
    echo "error: librime runtime expects $_opencc_required but it is missing under $_runtime_bin" >&2
    exit 1
  fi
  for _f in "$_bin"/libyaml-cpp.dll \
    "$_bin"/libleveldb.dll "$_bin"/libglog*.dll "$_bin"/libmarisa*.dll \
    "$_bin"/libgflags*.dll "$_bin"/libunwind.dll \
    "$_bin"/libwinpthread-1.dll; do
    [[ -f "$_f" ]] && cp -f "$_f" "$STAGE/bin/"
  done
  for _f in "$_bin"/liblua-*.dll "$_bin"/liblua.dll "$_bin"/lua5*.dll; do
    [[ -f "$_f" ]] && cp -f "$_f" "$STAGE/bin/"
  done
  shopt -u nullglob
  if [[ -f "$_runtime_bin/rime_deployer.exe" ]]; then
    cp -f "$_runtime_bin/rime_deployer.exe" "$STAGE/bin/"
  elif [[ -f "$_bin/rime_deployer.exe" ]]; then
    cp -f "$_bin/rime_deployer.exe" "$STAGE/bin/"
  fi
  # OpenCC config/data used by some Rime schemas
  if [[ -d "$_runtime_prefix/share/opencc" ]] && [[ "$_runtime_prefix" != "$_system_prefix" ]]; then
    rm -rf "$STAGE/share/opencc"
    mkdir -p "$STAGE/share"
    cp -a "$_runtime_prefix/share/opencc" "$STAGE/share/"
  elif [[ -d "$_runtime_prefix/share/opencc" ]] && [[ ! -d "$STAGE/share/opencc" ]]; then
    mkdir -p "$STAGE/share"
    cp -a "$_runtime_prefix/share/opencc" "$STAGE/share/"
  elif [[ -d /clang64/share/opencc ]] && [[ ! -d "$STAGE/share/opencc" ]]; then
    mkdir -p "$STAGE/share"
    cp -a /clang64/share/opencc "$STAGE/share/"
  elif [[ -d /mingw64/share/opencc ]] && [[ ! -d "$STAGE/share/opencc" ]]; then
    mkdir -p "$STAGE/share"
    cp -a /mingw64/share/opencc "$STAGE/share/"
  fi
}

copy_librime_dlls_from_msys() {
  local _runtime_prefix _bin _f
  _runtime_prefix="$(prepare_rime_runtime_prefix)"
  _bin="$_runtime_prefix/bin"
  [[ -d "$_bin" ]] || return 0
  mkdir -p "$STAGE/bin"
  shopt -s nullglob
  for _f in "$_bin"/librime-*.dll; do
    [[ -f "$_f" ]] && cp -f "$_f" "$STAGE/bin/"
  done
  shopt -u nullglob
}

generate_rime_pc() {
  local _version="$1" _prefix_native
  _prefix_native="$(native_path "$STAGE")"
  mkdir -p "$STAGE/lib/pkgconfig"
  cat >"$STAGE/lib/pkgconfig/rime.pc" <<EOF
prefix=${_prefix_native}
exec_prefix=${_prefix_native}
libdir=${_prefix_native}/lib
includedir=${_prefix_native}/include
pkgdatadir=${_prefix_native}/share/rime-data
pluginsdir=${_prefix_native}/lib/rime-plugins

Name: Rime
Description: Rime Input Method Engine
Version: ${_version#v}
Cflags: -I\${includedir}
Libs: -L\${libdir} -lrime
EOF
}

copy_librime_release_into_stage() {
  local _main _deps _version
  prepare_librime_release_root
  _main="$_LIBRIME_RELEASE_MAIN_DIR"
  _deps="$_LIBRIME_RELEASE_DEPS_DIR"
  _version="$_LIBRIME_RELEASE_VERSION"
  echo "==> [5a] librime: GitHub release package (${_version#v}, Windows-clang-x64)"
  mkdir -p "$STAGE/bin" "$STAGE/lib" "$STAGE/include" "$STAGE/share" "$STAGE/share/cmake"
  rm -f "$STAGE/bin"/librime*.dll "$STAGE/bin"/rime.dll
  rm -f "$STAGE/lib"/librime.dll "$STAGE/lib"/librime.dll.a "$STAGE/lib"/rime.lib
  cp -f "$_main/dist/lib/rime.dll" "$STAGE/bin/"
  cp -f "$_main/dist/lib/rime.lib" "$STAGE/lib/"
  cp -f "$_main/dist/include"/rime_*.h "$STAGE/include/"
  cp -f "$_main/dist/bin"/rime_*.exe "$STAGE/bin/"
  rm -rf "$STAGE/share/cmake/rime"
  cp -a "$_main/dist/share/cmake/rime" "$STAGE/share/cmake/"
  if [[ -d "$_deps/share/opencc" ]]; then
    rm -rf "$STAGE/share/opencc"
    cp -a "$_deps/share/opencc" "$STAGE/share/"
  fi
  generate_rime_pc "$_version"
}

verify_librime_in_stage() {
  local _source_desc="${1:-runtime}"
  if [[ ! -f "$STAGE/bin/rime.dll" ]]; then
    echo "error: no rime.dll under $STAGE/bin ($_source_desc)." >&2
    echo "  MINGW64: pacman -S mingw-w64-x86_64-librime mingw-w64-x86_64-librime-data" >&2
    exit 1
  fi
  if [[ ! -f "$STAGE/lib/rime.lib" ]]; then
    echo "error: no rime.lib under $STAGE/lib ($_source_desc)." >&2
    exit 1
  fi
  if [[ ! -f "$STAGE/include/rime_api.h" ]]; then
    echo "error: no rime_api.h under $STAGE/include ($_source_desc)." >&2
    exit 1
  fi
  if [[ ! -f "$STAGE/lib/pkgconfig/rime.pc" ]]; then
    echo "error: no rime.pc under $STAGE/lib/pkgconfig ($_source_desc)." >&2
    exit 1
  fi
  echo "==> [5a] librime ready in stage: rime.dll + rime.lib from $_source_desc"
}

if [[ -f "$RIME_SRC/CMakeLists.txt" ]]; then
  echo "==> [6] fcitx5-rime"
  if ! vendor_rime_share_into_stage; then
    echo "error: rime-data missing under stage and not found under /clang64 or /mingw64." >&2
    echo "  Install: pacman -S mingw-w64-clang-x86_64-librime-data (CLANG64; replaces old rime-data)" >&2
    exit 1
  fi
  copy_librime_release_into_stage
  verify_librime_in_stage "GitHub release package"
  rm -rf "$RIME_BUILD"
  PKG_CONFIG_PATH="$STAGE/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" \
    cmake -S "$RIME_SRC" -B "$RIME_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$STAGE" \
    -DCMAKE_PREFIX_PATH="$STAGE"
  cmake --build "$RIME_BUILD" -j"$JOBS"
  cmake --install "$RIME_BUILD" --prefix "$STAGE"
else
  echo "==> skip fcitx5-rime (RIME_SRC not set or missing CMakeLists.txt)"
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
echo "  bin/     — Fcitx5.exe, *.dll, fcitx5-x86_64.dll (if built), rime.dll + rime tools (GitHub release), lua if built"
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
