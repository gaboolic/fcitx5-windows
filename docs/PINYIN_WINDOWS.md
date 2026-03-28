# 在 Windows 上构建拼音（libime + fcitx5-chinese-addons）

本仓库的 TSF IME 与 `Fcitx5ImeEngine` 已能把按键交给 fcitx5 核心并显示候选，但 **拼音引擎不在本仓库内**，需要 **libime** 与 **fcitx5-chinese-addons** 编出的 addon DLL，并放到便携布局的 **`lib/fcitx5`**，数据在 **`share/fcitx5`**。

`find_package(Fcitx5Module …)` 依赖 **已安装** 的 CMake 包配置（`Fcitx5ModuleNotifications.cmake` 等在 **`$PREFIX/lib/cmake/`** 下）。因此 **不要指望** 在与本仓库同一次 CMake 配置里直接 `add_subdirectory(chinese-addons)` 而不先安装 fcitx5；推荐下面 **「先装 fcitx5-windows 到 prefix，再编 libime，再编 chinese-addons」** 的三段式流程。

## 依赖（MSYS2 Clang64 示例）

- `boost`（含 `iostreams`）、`libzstd`、`gettext`、`ninja`
- 已能完整配置本仓库的 Clang / CMake 环境

示例（CLANG64 终端）：`pacman -S --needed mingw-w64-clang-x86_64-boost mingw-w64-clang-x86_64-libzstd mingw-w64-clang-x86_64-gettext mingw-w64-clang-x86_64-ninja`

若 CMake 报找不到 **Boost**，多半是未装上述 `boost` 包或未在 **CLANG64** 环境内执行。

## 1. 准备源码目录

与 `fcitx5-windows` 平级（或自行设环境变量）：

- `../libime` — [libime](https://github.com/fcitx/libime)
- `../fcitx5-chinese-addons` — [fcitx5-chinese-addons](https://github.com/fcitx/fcitx5-chinese-addons)（与 libime / fcitx5 版本号在对方 `CMakeLists.txt` 里互相约束，请对齐 tag）

**libime 含 Git 子模块 [KenLM](https://github.com/kpu/kenlm)**（路径 `src/libime/core/kenlm`）。若 CMake 报缺少 `build_binary_main.cc` 或 `kenlm` 无源文件，在 **`libime` 仓库根目录**执行：

```bash
git submodule update --init --recursive
```

也可用 `scripts/prepare-pinyin-deps.sh` 只克隆 chinese-addons（libime 仍建议保持你正在开发的克隆）。

### 一键三段式（推荐）

在 **Clang64** 下设置好 `LIBIME_SRC`、`CHINESE_SRC`（默认分别为 `../libime`、`../fcitx5-chinese-addons`）后：

```bash
./scripts/build-pinyin-stage.sh
```

默认将 **`fcitx5-windows` 安装到 `./stage-pinyin`**，再在同一 prefix 下构建并安装 libime 与 chinese-addons。可用 **`STAGE`**、**`FCITX_BUILD`** 等环境变量覆盖路径（见脚本内注释）。

## 2. 安装本仓库（fcitx5 核心 + 模块）到统一 prefix

```bash
cd /path/to/fcitx5-windows
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
cmake --install build --prefix "$PWD/stage-pinyin"
```

之后应存在例如：

- `stage-pinyin/lib/cmake/Fcitx5Core/`
- `stage-pinyin/lib/cmake/Fcitx5Module/` 以及各 `Fcitx5Module*/`
- `stage-pinyin/lib/fcitx5/*.dll`（已有模块）
- `stage-pinyin/share/fcitx5/`

## 3. 构建并安装 libime 到同一 prefix

```bash
export STAGE=/path/to/fcitx5-windows/stage-pinyin
cmake -S ../libime -B build-libime -G Ninja \
  -DCMAKE_INSTALL_PREFIX="$STAGE" \
  -DCMAKE_PREFIX_PATH="$STAGE" \
  -DENABLE_TEST=OFF
cmake --build build-libime -j$(nproc)
cmake --install build-libime
```

完成后 `LibIMEPinyin` / `LibIMETable` 的 CMake 包应在 `stage-pinyin/lib/cmake/` 下。

## 4. 构建并安装 fcitx5-chinese-addons（最小 GUI / OpenCC / 云拼音）

```bash
cmake -S ../fcitx5-chinese-addons -B build-chinese -G Ninja \
  -DCMAKE_INSTALL_PREFIX="$STAGE" \
  -DCMAKE_PREFIX_PATH="$STAGE" \
  -DENABLE_GUI=OFF \
  -DENABLE_OPENCC=OFF \
  -DENABLE_BROWSER=OFF \
  -DENABLE_CLOUDPINYIN=OFF \
  -DENABLE_TEST=OFF
cmake --build build-chinese -j$(nproc)
cmake --install build-chinese
```

应得到 **`lib/fcitx5/libpinyin.dll`**（MSYS2/MinGW；addon 配置里 `Library=libpinyin`；少数 MSVC 布局可能是 `pinyin.dll`）、以及 punctuation、pinyinhelper 等依赖模块、`share/fcitx5` 下拼音相关数据。

若 `find_package` 失败，检查 **`CMAKE_PREFIX_PATH`** 是否仅指向 **`$STAGE`**（不要用未 `cmake --install` 的 build 树代替 prefix）。

## 5. 配置 profile（默认输入法含 pinyin）

- **`cmake --install`** 后，prefix 下会有 **`share/fcitx5/profile.pinyin.example`**（含 `keyboard-us`）与 **`profile.pinyin-only.example`**（仅 pinyin，无键盘回退）。
- 将其中之一复制到用户配置目录下的 **`profile`**（路径由 fcitx5 `StandardPaths` 决定，IME 与便携布局下一般为 **`…/config/fcitx5/profile`**）。若格式与当前 fcitx5 版本不兼容，可在 Linux 上添加一次拼音后拷贝生成的 `profile` 再对照修改。

临时调试也可设置环境变量 **`FCITX_TS_IM=pinyin`**（见 `Fcitx5ImeEngine::activatePreferredInputMethod`），无需完整 profile 亦可强制当前会话使用拼音。

## 6. 与 TSF IME 联调（拷贝 + 注册）

1. 将 **`stage-pinyin`** 当作安装根：保证 **`bin`** 含 IME DLL（`fcitx5-x86_64.dll` 或 MinGW 的 `libfcitx5-x86_64.dll`）、`Fcitx5Core.dll`、`Fcitx5Utils.dll`、可选 **`penguin.ico`**；**`lib/fcitx5`** 含拼音及依赖 addon；**`share/fcitx5`** 完整。
2. **安装 / 卸载**（**管理员** PowerShell；`DllRegisterServer` 写 **HKCR**）：

   ```powershell
   .\scripts\install-fcitx5-ime.ps1 -Stage D:\path\to\stage-pinyin -DeployDir C:\Fcitx5Portable
   .\scripts\uninstall-fcitx5-ime.ps1 -DeployDir C:\Fcitx5Portable
   ```

   - **仅删注册表**（文件已手动删掉时）：`.\scripts\uninstall-fcitx5-ime.ps1 -PurgeRegistryOnly`
   - **卸载并删除部署目录**：`.\scripts\uninstall-fcitx5-ime.ps1 -DeployDir C:\Fcitx5Portable -RemoveFiles`（可加 **`-Force`** 跳过确认）
   - **界面语言**：`-UICulture zh` 或 `en`（默认按系统 UI 自动选）
   - 兼容旧入口：**`deploy-ime-stage.ps1`**（安装同参数；**`-Unregister`** / **`-RemoveFiles`** / **`-Force`** 卸载）
   - MSYS：`./scripts/install-fcitx5-ime.sh` / `uninstall-fcitx5-ime.sh` / `deploy-ime-stage.sh`（参数同上）
3. 在 **设置 → 时间和语言 → 语言和区域** 中为 **中文（简体）** 添加键盘 **Fcitx5**，在 **记事本** 中选中该 IME，**Ctrl+Space** 进入中文模式后输入拼音，确认候选与上屏。

**说明**：单独运行 **`Fcitx5.exe`** 无法替代在应用内的 TSF 输入体验；测拼音请以 **IME + 宿主应用** 为准。

## 7. keyboard-us 回退（可选）

profile 中常见 **`keyboard-us`**。本仓库根目录默认 **`ENABLE_KEYBOARD=OFF`**，且启用键盘引擎需要 **XKBCommon** 等（MSYS2 需安装对应开发包）。若暂无键盘引擎，可只在分组里保留 **pinyin**（及 table 等），或开启 `ENABLE_KEYBOARD` 并解决依赖后再装 **keyboard** addon。

## 已知注意

- `cmake --install` 时若出现部分路径落到 **`C:/share/...`**，与根目录 `CMAKE_INSTALL_PREFIX` 设为 `C:` 有关；建议使用 **`--prefix` 指向显式目录**（如上面的 `stage-pinyin`）。
- IME 与独立 **`Fcitx5.exe`** 同写 **`profile`** 时仍可能竞态，测试时尽量只开一端或分配置目录。
