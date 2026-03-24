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

4. **拼音 / 中文输入引擎**（未集成 — **要测真实拼音必须先做**）
   - [ ] 将 **libime** 与 **fcitx5-chinese-addons**（或等价）纳入本仓库 CMake：在 Windows 上编出 **pinyin（及所需 table）** 等 addon **`*.dll`**，并安装到便携布局的 **`lib/fcitx5`**，数据与 mo 等到 **`share/fcitx5`**
   - [ ] 默认 **`profile` / 分组**：至少包含可用 **`pinyin`**（或明确文档化 `FCITX_TS_IM=pinyin`）；与现有激活顺序（经 `keyboard-us` 回退）一致且无「空分组」
   - [ ] **`keyboard`（`keyboard-us`）回退**（可选但与 profile 常见配置强相关）：根目录 **`ENABLE_KEYBOARD=ON`** 时能在目标环境找到 **XKBCommon** 等依赖（MSYS2 需装对应包；MSVC 需可解析的 SDK/三方库）；或改为不依赖 XKB 的英文回退方案并写进文档
   - [ ] **验证路径**：在 **记事本等** 中启用 **TSF IME**（非单独 `Fcitx5.exe`）→ Ctrl+Space 中文 → 拼音上屏与候选与 P0 候选 UI 一致；补充最短「构建 + 部署目录 + 注册」说明（可与 P1-5 安装脚本合并）

**工程说明（2025-03）**：`win32/tsf` 已改为 **WRL `ComPtr`**（`<wrl/client.h>`），不再依赖 ATL，便于 VS Build Tools 无 ATL 组件时编译；**仅 MSVC** 可构建独立 `win32/`（无 `Fcitx5Core`，IME 用 Stub）。**带 fcitx 核心的 IME**：在仓库根目录与 `fcitx5` 一起配置 CMake（`FCITX5_WINDOWS_BUILD_WIN32_IME=ON`），使 `tsf` 链接 `Fcitx5::Core` 并编译 `Fcitx5ImeEngine.cpp`。**MSYS/Clang 一键连编**：`scripts/build-msys-full.sh`（配置 + `ninja` + `ctest`；`SKIP_TEST=1` 可跳过测试）；构建后 **`win32/dll` POST_BUILD** 将 `Fcitx5Core` / `Fcitx5Utils` 复制到与 `fcitx5-x86_64.dll` 同目录；`cmake --install` 时上述 DLL 与 IME 一并安装到 `CMAKE_INSTALL_BINDIR`（便携布局与 `Fcitx5.exe` 同 `bin`）。

### P1 - 重要功能

5. **安装/卸载脚本完善**（拼音实测强依赖「一次拷全」）
   - 完整的注册表清理逻辑（含 **TSF / IME CLSID**）
   - 依赖文件复制：**`fcitx5-x86_64.dll`**、**`Fcitx5Core` / `Fcitx5Utils`**、**`lib/fcitx5` 下全部 addon**、**`share/fcitx5`**、可选 **`Fcitx5.exe`**；与 CMake `install` / POST_BUILD 规则对齐并可在 CI 校验
   - 多语言支持

6. **配置管理界面**
   - 输入法候选数量设置
   - 皮肤主题配置
   - 快捷键配置

7. **输入法切换功能**
   - 实现 ITfInputProcessorProfile
   - 支持 Ctrl+Space 切换中英文（TSF 层已有，可与系统语言栏行为对齐）
   - 支持 Shift 切换输入法

### P2 - 增强功能

8. **状态栏托盘图标**
   - 显示当前输入法状态
   - 右键菜单（切换输入法、退出）
   - 点击切换功能

9. **拼写纠错支持**
   - 集成 fcitx5 spell 模块
   - 单词拼写检查

### P3 - 优化功能

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
    - 自定义候选窗口样式
    - 预设主题