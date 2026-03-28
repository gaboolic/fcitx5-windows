# 在 Windows 上构建拼音（libime + fcitx5-chinese-addons）

本仓库的 TSF IME 与 `Fcitx5ImeEngine` 已能把按键交给 fcitx5 核心并显示候选，但 **拼音引擎不在本仓库内**，需要 **libime** 与 **fcitx5-chinese-addons** 编出的 addon DLL，并放到便携布局的 **`lib/fcitx5`**，数据在 **`share/fcitx5`**。

`find_package(Fcitx5Module …)` 依赖 **已安装** 的 CMake 包配置（`Fcitx5ModuleNotifications.cmake` 等在 **`$PREFIX/lib/cmake/`** 下）。因此 **不要指望** 在与本仓库同一次 CMake 配置里直接 `add_subdirectory(chinese-addons)` 而不先安装 fcitx5；推荐下面 **「先装 fcitx5-windows 到 prefix，再编 libime，再编 chinese-addons」** 的三段式流程。

## 依赖（MSYS2 Clang64 示例）

- `boost`（含 `iostreams`）、`libzstd`、`gettext`、`ninja`
- 已能完整配置本仓库的 Clang / CMake 环境

示例（CLANG64 终端）：`pacman -S --needed mingw-w64-clang-x86_64-boost mingw-w64-clang-x86_64-libzstd mingw-w64-clang-x86_64-gettext mingw-w64-clang-x86_64-ninja`

若还要编 **fcitx5-rime**、**fcitx5-lua**（与 CI 安装包一致），额外安装：**`mingw-w64-clang-x86_64-librime-data`**（MSYS2 上已取代旧包名 **`rime-data`**）、**`opencc`**、**`lua`**，以及从源码编 **librime** 所需的 **`yaml-cpp`、`leveldb`、`glog`、`gflags`、`marisa`**；CI 仍安装 **`mingw-w64-clang-x86_64-librime`**，以便合并构建失败时回退到系统 **`rime.pc` / DLL**。脚本会把 **`share/rime-data`**（安装路径未变）、依赖 DLL 与 **Lua** 拷进 **`bin/`**。

**Rime 核心里的 Lua（librime-lua）** 与 [fcitx5-prebuilder](https://github.com/fcitx-contrib/fcitx5-prebuilder) / macOS 插件链一致：把 [librime-lua](https://github.com/hchunhui/librime-lua) 拷到 **`librime/plugins/lua`**，再对 **librime** 打开 **`BUILD_MERGED_PLUGINS=ON`**，把插件 **合并进同一个 `librime-*.dll`**（装到 **`$STAGE/bin`**），随后 **`PKG_CONFIG_PATH`** 优先用 **`$STAGE/lib/pkgconfig`** 再编 **fcitx5-rime**。源码目录默认为 **`../librime`**（建议与 MSYS2 包同版本，如 **1.14.0**）、**`../librime-lua`**，可用 **`LIBRIME_SRC`**、**`LIBRIME_LUA_SRC`** 覆盖；若缺少这两棵树，则在 MSYS 上会回退为拷贝预装的 **`librime-*.dll`**（无 Rime 内置 Lua）。

**fcitx5-table-extra** 只需基础依赖（在 **chinese-addons 已安装到 prefix** 之后编）。

若 CMake 报找不到 **Boost**，多半是未装上述 `boost` 包或未在 **CLANG64** 环境内执行。

## 1. 准备源码目录

与 `fcitx5-windows` 平级（或自行设环境变量）：

- `../libime` — [libime](https://github.com/fcitx/libime)
- `../fcitx5-chinese-addons` — [fcitx5-chinese-addons](https://github.com/fcitx/fcitx5-chinese-addons)（与 libime / fcitx5 版本号在对方 `CMakeLists.txt` 里互相约束，请对齐 tag）
- （可选，与官方 CI 安装包一致）`../fcitx5-table-extra`、`../fcitx5-rime`、`../fcitx5-lua`、`../librime`（tag 如 1.14.0）、`../librime-lua` — **`TABLE_EXTRA_SRC`**、**`RIME_SRC`**、**`LUA_SRC`**、**`LIBRIME_SRC`**、**`LIBRIME_LUA_SRC`** 可覆盖路径；见 `scripts/build-pinyin-stage.sh`

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

默认将 **`fcitx5-windows` 安装到 `./stage-pinyin`**，再在同一 prefix 下构建并安装 libime、chinese-addons，以及在源码存在时的 **fcitx5-table-extra**、**fcitx5-rime**、**fcitx5-lua**。可用 **`STAGE`**、**`FCITX_BUILD`** 等环境变量覆盖路径（见脚本内注释）。

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

- **`cmake --install`** 后，prefix 下会有：
  - **`profile.windows.example`** — 图形安装程序默认种子：**拼音 + 五笔（`wbx`）+ 中州韵（`rime`）**（`wbx`/`rime` 需对应 addon 已在 stage），无 `keyboard-us`（与常见未启用 keyboard 引擎的构建一致）。若本地未克隆 **fcitx5-rime**，请删掉 **`Name=rime`** 一段，或改用 **`profile.pinyin-only.example`**。未克隆 **fcitx5-lua** 仅缺少 Lua 相关 addon，不必改 profile。
  - **`profile.pinyin.example`** — 拼音 + `keyboard-us`（需已安装 keyboard addon）。
  - **`profile.pinyin-only.example`** — 仅 pinyin。
- 将其中之一复制到用户配置目录下的 **`profile`**（路径由 fcitx5 `StandardPaths` 决定，IME 与便携布局下一般为 **`%AppData%\Fcitx5\config\fcitx5\profile`**）。若格式与当前 fcitx5 版本不兼容，可在 Linux 上添加一次拼音后拷贝生成的 `profile` 再对照修改。

### 托盘「切换输入法」里只有拼音？

托盘菜单（以及 TSF 侧 `readProfileInputMethodsFromConfig`）**只列出当前分组**里 **`[Groups/0/Items/N]` 的 `Name=`**，与 Linux 上 fcitx5 行为一致。若 profile 里只有 `pinyin`，菜单里就不会出现五笔或中州韵。

- **五笔**：在 profile 的同一分组中增加一项 **`Name=wbx`**（与 `share/fcitx5/inputmethod/wbx.conf` 对应；需已安装 **table** 引擎及词库）。新安装包会优先用 **`profile.windows.example`** 种子（仅当用户目录下尚无 `profile` 时）。**已装过旧版**的用户若已有 `profile`，需自行编辑该文件，或删掉后重装/从 `share\fcitx5\profile.windows.example` 复制一份。
- **中州韵（Rime）**：需要 **fcitx5-rime**、`lib/fcitx5` 下的 rime addon DLL、**`share/rime-data`**（以及 `bin` 侧 **librime** 等运行时 DLL，脚本会从 CLANG64 的 `bin` 拷入）。**CI 打包 job** 已克隆 **fcitx5-rime**、**fcitx5-table-extra**、**fcitx5-lua** 并随 **`build-pinyin-stage.sh`** 写入 stage；本地为 Rime 需 **`mingw-w64-clang-x86_64-librime-data`**（数据仍在 **`share/rime-data`**）与 **`RIME_SRC`**，为 Lua 需 **`lua` 包** 与 **`LUA_SRC`**。profile 中需有 **`Name=rime`**（**`profile.windows.example`** 已含）。托盘在「当前输入法为 rime」时才会显示「重新部署中州韵」等扩展项。

### 拼音候选顺序 / 词频「不对」？

**与安装到 `C:\Program Files\Fcitx5` 的关系**：只读安装目录**不会**单独把「静态词库」弄坏；TSF 在宿主进程里**可以正常读** Program Files 下的 `share\`。更常见的是下面两类原因。

**1. 用户目录里的文件「盖住」了安装目录里的系统词库（很像静态词频不对）**

fcitx5-chinese-addons 的拼音引擎加载 **libime 系统主词库**时用的是 **`StandardPathsType::Data`** 下的相对路径 **`libime/sc.dict`**（见上游 `im/pinyin/pinyin.cpp`：`standardPath.open(StandardPathsType::Data, "libime/sc.dict")`）。在 Windows 的 `StandardPaths` 里，**`Data` 类型**的查找顺序大致是：

1. **`%AppData%\Fcitx5\data\`**（Roaming，先查）
2. **`%LocalAppData%\Fcitx5\data\`**
3. 安装根下的 **`share\`**（例如 **`C:\Program Files\Fcitx5\share\libime\sc.dict`**）

**谁先存在就用谁**。因此只要 Roaming 或 Local 里有一份 **`Fcitx5\data\libime\sc.dict`**（例如从旧版/便携目录拷过、不完整、或版本不匹配），就会**一直优先于** Program Files 里的正式词库，表现会像「整套静态词频都不对」，而**不是**个人用户词的问题。

同理，扩展词库在 **`StandardPathsType::PkgData`** 的 **`pinyin/dictionaries/*.dict`** 上按目录顺序合并，**同名文件**也是「**用户侧先出现者优先**」（Roaming 下的 `fcitx5` 与 `Fcitx5` 在 Windows 上通常指向同一文件夹，仅子路径不同）。若 `%AppData%\…\fcitx5\pinyin\dictionaries\` 里残留旧 `.dict`，也会覆盖安装目录里同名词典。

**建议排查**（可先退出使用 IME 的应用后再试）：

- 若存在且怀疑有问题，可**改名或删除**后重启应用，让引擎回退到 Program Files 内建文件：  
  - `%AppData%\Fcitx5\data\libime\sc.dict`  
  - `%LocalAppData%\Fcitx5\data\libime\sc.dict`  
  - `%AppData%\fcitx5\pinyin\dictionaries\`（或 `Fcitx5\…` 下同路径）下的可疑 `.dict`

**2. CI / 安装包候选顺序异常，便携本机构建却正常（例如 `xiexieni` 顶不上「谢谢你」）**

libime 通过 **`DefaultLanguageModelResolver`** 找 **`zh_CN.lm`**（装在前缀的 **`lib/libime/zh_CN.lm`**）。若未设置环境变量 **`LIBIME_MODEL_DIRS`**，解析器会使用 **CMake 配置时的绝对路径** `LIBIME_INSTALL_LIBDATADIR`（即当时 `CMAKE_INSTALL_PREFIX/lib/libime`）。  
CI 上该前缀往往是 `D:/a/.../stage-installer` 等，**用户机器上不存在**，语言模型文件打不开，解码器相当于没有正常 n-gram 打分，就会出现「词都能出，但顺序完全不对」。  
本地便携构建若 **`CMAKE_INSTALL_PREFIX` 正好等于你的便携目录**，嵌入路径仍存在，就会「只有安装包坏、便携好」的现象。

**处理**：本仓库已在 TSF / `Fcitx5.exe` 启动时设置 **`LIBIME_MODEL_DIRS`** 指向安装根下的 **`lib/libime`**，并在 **`scripts/build-pinyin-stage.sh`** 里对上游 **`languagemodel.cpp`** 打补丁：在 **`_WIN32`** 下用 **`;`** 分隔 `LIBIME_MODEL_DIRS`（上游用 **`:`** 会把 `C:\...` 拆坏）。重新走完整 stage + 安装包流程后应恢复。若你自行编 libime 而未用该脚本，需同步补丁或等上游修复。

**3. 新装 / 无云端的正常差异**

新安装**没有**你在其他机器上积累的个人词频与用户词库时，排序主要来自**当前加载到的那套**系统词库与默认策略。本仓库构建 chinese-addons 时通常关闭 **云拼音**（`ENABLE_CLOUDPINYIN=OFF`），与带云端纠偏的发行版也会不同。

随使用时间增长，**用户词与用户语言模型**等会写入 **`%AppData%` / `%LocalAppData%`** 下 Fcitx5 / fcitx5 相关路径（例如 **`pinyin/user.dict`**、**`pinyin/user.history`** 等）；若用户目录不可写，学习结果无法保存。

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
