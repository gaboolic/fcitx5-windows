# Fcitx5 Windows 开发待办事项

## 按重要性排序

### P0 - 核心功能 (阻塞发布)

1. **候选词 UI / 候选窗口**（已完成）
   - [x] 自定义 `WS_POPUP` 候选窗：序号、`↑`/`↓` 高亮、鼠标点选
   - [x] 候选窗锚点：`ITfContext::EnumViews` + `ITfContextView::GetTextExt`（失败则回退 `GetCaretPos`）；窗口限制在显示器 **工作区内**
   - [x] `ITfUIElement` / `ITfCandidateListUIElement`：`ITfUIElementMgr::Begin/Update/End` + `CandidateListUiElement`（与自定义候选窗并行）
   - [x] 键盘选词：`1`–`9`、`0`（第 10 项）、空格确认高亮项、回车确认

2. **完整的输入逻辑集成**（已完成）
   - [x] 按键经 `ITfKeyEventSink` → 同步 `ITfEditSession` 更新组合/preedit
   - [x] **Ctrl+Space** 切换中文/英文（中文模式下拦截字母作拼音缓冲）
   - [x] 中文模式下 **Shift+字母** 不拦截，交给应用（临时英文/大写）
   - [x] **`ImeEngine` 接口**：`win32/tsf/ImeEngine.h`；默认 `makeStubImeEngine()`（`StubImeEngine.cpp`）演示候选
   - [x] **`Fcitx5ImeEngine`**：`TsfInputContext` + `Instance` / `InputContext::keyEvent`；失败则回退 Stub（仅在与 `Fcitx5Core` 同 CMake 构建时启用）

3. **与 fcitx5 addon 的完整集成**（已完成）
   - [x] IME 进程内 **`registerDefaultLoader(nullptr)`** 动态加载 `FCITX_ADDON_DIRS`（与 `Fcitx5.exe` 相同布局：`安装根/lib/fcitx5`、`share/fcitx5`）；环境变量按 **IME DLL 路径** 推导（`…/bin/*.dll` → 安装根）
   - [x] 新引擎实现 `ImeEngine`，由 `Tsf` 持有 `std::unique_ptr<ImeEngine>`（`Tsf(std::unique_ptr<ImeEngine>)` 可注入；默认优先 `Fcitx5ImeEngine`，失败 Stub）
   - [x] 输入法列表与切换与 `InputMethodManager` / `profile` 对齐（`Fcitx5ImeEngine`）
     - 激活顺序：`FCITX_TS_IM`（若存在且合法）→ 当前分组 `defaultInputMethod` → 分组 `inputMethodList` 首项可用 → `keyboard-us` → `foreachEntries` 第一个可用项
     - 热键：`GlobalConfig` 的「切换输入法 / 切换分组」经 `EditSession` → `imManagerHotkeyWouldEat` / `tryConsumeImManagerHotkey`，对应 `Instance::enumerate(ic, …)` 与 `InputMethodManager::enumerateGroup`；**Ctrl+Space** 仍仅用于 TSF 层中英切换
     - 与 `Fcitx5.exe` 共享策略：标准路径锚定 IME DLL（`mainInstanceHandle`），同安装根下共用 `config`/`data`；与独立 `Fcitx5.exe` 同时写 `profile` 时仍有竞态可能

4. **拼音 / 中文输入引擎**（脚本与部署链就绪；单树 CMake / CI 仍可选加强）
   - [x] **文档与脚本**：`docs/PINYIN_WINDOWS.md`；**`scripts/01-build-fcitx5-windows.ps1`** + **`scripts/02-build-deps.sh`**（`install` → libime → chinese-addons 等至同一 **`STAGE`**）；`contrib/fcitx5/profile.pinyin.example` / **`profile.pinyin-only.example`**
   - [x] **可重复构建（脚本级）**：见 **`02-build-deps.sh`** 与 **`01-build-fcitx5-windows.ps1`**；**`ExternalProject` / 单 CMake 树** 或 **CI job** 校验仍可选加强
   - [x] **默认 profile 示例与文档化**：**`cmake --install`** 安装 **`share/fcitx5/profile*.example`**；**`FCITX_TS_IM=pinyin`** 已写入文档与引擎逻辑；**运行时自动写入用户 profile** 仍可选
   - [x] **`keyboard-us` 回退路径**：无键盘引擎时用 **`profile.pinyin-only.example`**；需 **keyboard-us** 时 **`ENABLE_KEYBOARD` + XKB** 见 **`PINYIN_WINDOWS.md`**
   - [x] **验证路径**：**`scripts/install-fcitx5-ime.ps1`**（= **`04-deploy-to-portable.ps1`** + **`05-register-ime.ps1`**）/ **`scripts/uninstall-fcitx5-ime.ps1`**；**`docs/PINYIN_WINDOWS.md` §6**；**`03-build-installer.ps1`** / **`04-deploy-to-portable.ps1`**

**工程说明（2025-03）**：`win32/tsf` 已改为 **WRL `ComPtr`**（`<wrl/client.h>`），不再依赖 ATL，便于 VS Build Tools 无 ATL 组件时编译；**仅 MSVC** 可构建独立 `win32/`（无 `Fcitx5Core`，IME 用 Stub）。**带 fcitx 核心的 IME**：在仓库根目录与 `fcitx5` 一起配置 CMake（`FCITX5_WINDOWS_BUILD_WIN32_IME=ON`），使 `tsf` 链接 `Fcitx5::Core` 并编译 `Fcitx5ImeEngine.cpp`。在 **MSYS2 CLANG64** 下可 **`cmake -B build -G Ninja`**、**`cmake --build`**、**`cmake --install --prefix …/stage`**（或 **`scripts/01-build-fcitx5-windows.ps1`**）；构建后 **`win32/dll` POST_BUILD** 将 `Fcitx5Core` / `Fcitx5Utils` 复制到与 `fcitx5-x86_64.dll` 同目录；`cmake --install` 时上述 DLL 与 IME 一并安装到 `CMAKE_INSTALL_BINDIR`（便携布局与 `Fcitx5.exe` 同 `bin`）。**拼音 + 部署**：**`01` / `02` / `03` / `04` / `05`**、**`install-fcitx5-ime.ps1` / `uninstall-fcitx5-ime.ps1`**、**`docs/PINYIN_WINDOWS.md`**；**`cmake install`** 附带 **`share/fcitx5/profile.pinyin*.example`** 与可选 **`bin/penguin.ico`**。

### P1 - 重要功能

5. **安装/卸载脚本完善**（便携脚本 + 图形安装包）
   - [x] **安装**：**`scripts/install-fcitx5-ime.ps1`**（**robocopy** + **`regsvr32 /s`**）
   - [x] **卸载**：**`scripts/uninstall-fcitx5-ime.ps1`** — **`regsvr32 /u`** 后 **删除 `HKCR\CLSID\{FC3869BA-…}`** 与 **`HKLM\SOFTWARE\Microsoft\CTF\TIP\{…}`**（与 **`register.cpp`** 一致）；**`-PurgeRegistryOnly`**（无 DLL）；**`-RemoveFiles`** 删部署树
   - [x] **多语言**：**`-UICulture zh|en|auto`**（提示语中英）
   - [x] **HKCU 残留**：**`HKCU\…\CTF\TIP\{CLSID}`** / **`HKCU\…\Classes\CLSID\{CLSID}`** 可由 **`uninstall-fcitx5-ime.ps1 -PurgeHkcu`** 删除（可与 **`-PurgeRegistryOnly`** 联用）；**`Fcitx5-Ime.Common.ps1`**：`Get-FcitxImeHkcuResidualRegistryPaths` / `Remove-FcitxImeHkcuResidualRegistry`
   - [x] **CI**：**`win32` job** 运行 **`scripts/validate-powershell-scripts.ps1`**（语法解析，不执行）
   - [x] **图形安装向导（Inno Setup 6）**：**`installer/Fcitx5Tsf.iss`** + **`installer/build-installer.ps1 -StageDir …`** → **`installer/dist/Fcitx5TSF-Setup.exe`**（可选安装目录、中英向导语言）；安装目录内 **`unins000.exe`** = 卸载程序，且登记到「应用和功能」；说明见 **`installer/README.txt`**

6. **配置管理界面**
   - [x] **入门**：在资源管理器打开 **`%AppData%\Fcitx5`**；编辑 **`config\fcitx5\profile`** 等；运行便携目录 **`bin\fcitx5-config-win32.exe`** 做图形设置（从 **`bin`** 启动或已加入 PATH）
   - [x] **简易图形设置（对齐 Linux `GlobalConfig` + `profile`）**：**`win32/configui/fcitx5-config-win32.exe`** — 读写 **`conf/fcitx5/config`**（`readAsIni` / `safeSaveAsIni`）与 **`profile`**；候选每页数量、常用 **Behavior/Hotkey** 开关与热键字符串（fcitx 可移植语法）；程序须在 **`bin/`** 下运行以便 **`mainInstanceHandle`** 与 TSF 布局一致；构建需 **`Fcitx5Core`**（与 IME 同盘CMake）；Inno 开始菜单 **Settings** 快捷方式已链到该 exe
   - [x] 逐键录制快捷键：**`fcitx5-config-win32`** 各热键行 **Record** + **`WH_KEYBOARD_LL`**（追加 / 完成写回编辑框；Esc 取消）；仍可与手写 fcitx 可移植语法并用

7. **输入法切换功能**
   - [x] **语言配置**：由 **`register.cpp`** 调用 **`ITfInputProcessorProfileMgr::RegisterProfile`** 注册 TIP；**由系统提供 `ITfInputProcessorProfile`**，IME 实现 **`ITfTextInputProcessor(Ex)`** 而非自实现 Profile 接口
   - [x] **Ctrl+Space**：TSF 层中英切换（**`EditSession.cpp`**）；与 fcitx **`GlobalConfig` 热键**并行（**`Fcitx5ImeEngine`**）
   - [x] **修饰键热键（含默认 Shift_L / Super+space 等）**：向 **`Instance` 投递 `KeyEvent` 的按下与抬起**（**`KeyEventSink` + `deliverFcitxRawKeyEvent`**），以支持 **`altTriggerKeys` / `enumerateGroup*`** 等需 **KeyUp** 的逻辑

**已处理（用户反馈）**

- [x] **中文模式下逗号、句号无效果**：根因是无 composition 时 **`endCompositionCommit`** 仅在 **`compositionRange_`** 上 `SetText`，提交被丢弃。已在 **`EditSession.cpp`** 对无组合范围路径使用 **`ITfInsertAtSelection::InsertTextAtSelection`** 插入；**`VK_DECIMAL`**（小键盘 `.`）纳入标点转发；**`keyFromWindowsVk`** 映射 **`FcitxKey_KP_Decimal`**
- [x] **Shift 无法切换中/英文**：**`KeyEventSink.cpp`** 中单次 **Shift** 点按会在 **KeyUp** 触发 **`langBarScheduleToggleChinese`**；除忽略 **Shift 自动重复** 外，也兼容只调用 **`OnKeyDown/Up`**、不稳定调用 **`OnTestKey*`** 的 TSF 宿主，并对同一次按键的 **`OnTest*`/`OnKey*`** 双回调去重
- [x] **托盘进「隐藏的图标」**：**`LangBarTray.cpp`** — **`GetInfo`** 重新启用 **`TF_LBI_STYLE_SHOWNINTRAY`**；**`Shell_NotifyIcon`** 增加 **`NIF_SHOWTIP`**（与 **`NIF_GUID` + `NIM_SETVERSION`** 并用）。Win11 仍可能默认进溢出区，需用户 **拖拽图标到任务栏** 固定 behavior 与多数 TIP 一致

### P2 - 增强功能

8. **状态栏托盘图标**
   - [x] **实现**：**`ITfLangBarItemButton`** + **`TF_LBI_STYLE_SHOWNINTRAY`**（**`LangBarTray.cpp`**），激活时 **``ITfLangBarItemMgr::AddItem``**；图标为 **`penguin.ico`**（与 TIP 注册一致）；**左键**同步 **Ctrl+Space** 的中/英切换（**`langBarScheduleToggleChinese`** + **``RequestEditSession``**）；**右键**菜单：中文 / 英文、**Fcitx5 设置…**（启动 **`fcitx5-config-win32.exe`**）、打开 **`%AppData%\Fcitx5`**；**`MsctfMingwCompat`** 补全 MinGW 缺的 **`ITfLangBarItemButton`** / **`TF_LBI_*`**；链接 **``shell32``**（**`ShellExecute`** / **`SHGetFolderPath`**）
   - [x] **默认可见性**：已加强 **`TF_LBI_STYLE_SHOWNINTRAY`** + **`NIF_SHOWTIP`**；若系统仍将新图标放入溢出区，由用户在任务栏 **「显示隐藏的图标」→ 拖到主栏** 固定（与 Weasel 等一致）
   - [x] **QQ 录屏保存时崩溃**：场景为 **QQ 录屏 + 记事本内用 Fcitx5 打字 + 最后保存**；继续定位 **`libglog-2.dll` / Rime / TSF 宿主** 在 QQ 进程内的崩溃链
  - [x] **`Shift + 字母` 交给 IME**：像 **`Shift + A`** 这类组合也先交给 IME 处理，避免宿主过早吞掉按键（**`EditSession`** 按 Shift/Caps 映射大写；**`appendLatinLowercase`** 发送 **`FcitxKey_A`–`Z`** keysym，与 Weasel 的 SHIFT+字母一致）
  - [x] **候选框跟随光标**：**`EditSession.cpp`** — 与 **Weasel** `Composition.cpp` 一致：**`GetTextExt`** 使用**折叠到预编辑光标**的 range（非整段 composition）；**EnumViews** 结果将**与前台 HWND 关联**的 view 优先（等价于 `GetActiveView`）；**`tsf-trace.log`** 中检索 **`candidatePos`** 可看到各步 **GetTextExt** 矩形、**rangeMode**、兜底路径
- [x]另外输入法换到非fcitx5的时候 托盘图标也一直在；切换输入法资源管理器重启
- [x] **fcitx5-tray-helper.exe 为唯一托盘 owner**：TSF 经 WM_COPYDATA 发送 **Focus**（ActivateEx 会话）、**Ui**（引擎可见）、**Status**（中/英、IM、菜单动作）；**legacy Snapshot** 仍由 helper 兼容。Explorer 不再做托盘显示/隐藏裁决（`langBarNotifyIconUpdate` 仅 `explorerTrayHelperPrimedOnce`）。helper 合并 **GetForegroundWindow** PID、tip 会话 PID 与 **Ui/Status**，并记录前台有效性（`g_foregroundTrayValidity`）；是否用前台收紧托盘策略留待后续
- [x] 中英文的状态显示在托盘（`TF_LBI_STYLE_SHOWNINTRAY` + 普通进程 `initShellTrayHostForMessages` 避免与 `Shell_NotifyIcon` 重复）

### P3 - 优化功能

9. **拼写纠错支持**
   - 集成 fcitx5 spell 模块
   - 单词拼写检查
   - 说明：依赖 **spell addon** 与同类词典部署，与桌面 fcitx5 一致；TSF 层无额外钩子

10. **性能优化**
   - 减少 key event 延迟
   - 候选词预加载

11. **日志和调试功能**
    - 调试日志输出
    - 崩溃报告

12. **多语言支持**
    - 简体/繁体中文
    - 其他语言界面

13. **皮肤主题系统**
   - [ ] **完整 fcitx5-config-Qt**（各插件分页、主题预览等）
   - [ ] 皮肤主题配置
    - 自定义候选窗口样式
    - 预设主题