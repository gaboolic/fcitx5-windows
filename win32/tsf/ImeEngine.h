#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fcitx {

struct ProfileInputMethodItem {
    std::string uniqueName;
    std::wstring displayName;
    bool isCurrent = false;
};

struct TrayStatusActionItem {
    std::string uniqueName;
    std::wstring displayName;
    bool isChecked = false;
};

// Pluggable input logic (pinyin → preedit/candidates). TSF layer only displays
// and commits strings; replace Stub with fcitx5/libime-backed engine later.
class ImeEngine {
  public:
    virtual ~ImeEngine() = default;

    /// Refresh cached preedit/candidates from IME (fcitx InputPanel); no-op for
    /// stub.
    virtual void syncInputPanelFromIme() {}

    virtual void clear() = 0;

    virtual const std::wstring &preedit() const = 0;
    /// Caret offset in UTF-16 code units within `preedit()` (TSF composition).
    virtual int preeditCaretUtf16() const {
        return static_cast<int>(preedit().size());
    }
    virtual const std::vector<std::wstring> &candidates() const = 0;
    virtual int highlightIndex() const = 0;
    virtual void setHighlightIndex(int index) = 0;

    virtual void appendLatinLowercase(wchar_t ch) = 0;
    virtual void backspace() = 0;
    /// delta +1 / −1; wraps when candidates non-empty.
    virtual void moveHighlight(int delta) = 0;

    virtual bool hasCandidate(size_t index) const = 0;
    virtual std::wstring candidateText(size_t index) const = 0;
    virtual std::wstring highlightedCandidateText() const = 0;

    /// Commits produced by fcitx core (`InputContext::commitString`) between
    /// TSF edit-session steps. Default: none (stub engine).
    virtual std::wstring drainNextCommit();

    /// If true, mouse pick is handled by the engine (fcitx digit keys). If
    /// false, TSF commits `candidateText(index)` (stub).
    virtual bool feedCandidatePick(size_t index);

    /// Forward Up/Down/Space/Return/digit to fcitx when the candidate window is
    /// up.
    virtual bool tryForwardCandidateKey(unsigned vk);
    /// Forward Return to fcitx when there is preedit but no candidate list.
    virtual bool tryForwardPreeditCommit();

    /// Keys bound in global `profile` (enumerate IM / enumerate group). Stub:
    /// never.
    virtual bool imManagerHotkeyWouldEat(unsigned vk,
                                         std::uintptr_t lParam) const;
    /// Apply hotkey if it matches `GlobalConfig`; returns true if the key was
    /// consumed.
    virtual bool tryConsumeImManagerHotkey(unsigned vk, std::uintptr_t lParam);

    /// Modifier-only shortcuts (`triggerKeys` / `altTriggerKeys` / …) need
    /// KeyDown+KeyUp delivered as `fcitx::KeyEvent`; TSF otherwise never calls
    /// `OnKeyUp`. Stub: false.
    virtual bool fcitxModifierHotkeyUsesFullKeyEvent(unsigned vk) const;
    /// Forward one key to fcitx `Instance` key watcher; returns
    /// KeyEvent::accepted().
    virtual bool deliverFcitxRawKeyEvent(unsigned vk, std::uintptr_t lParam,
                                         bool isRelease);

    /// List input methods in the current profile group for tray menus.
    virtual std::vector<ProfileInputMethodItem> profileInputMethods() const;
    /// Activate one input method from the current profile group.
    virtual bool activateProfileInputMethod(const std::string &uniqueName);
    /// Query the current active input method unique name.
    virtual std::string currentInputMethod() const;
    /// List checkable status-area actions for tray menus.
    virtual std::vector<TrayStatusActionItem> trayStatusActions() const;
    /// Activate one status-area action by unique name.
    virtual bool activateTrayStatusAction(const std::string &uniqueName);
    /// Reload pinyin addon config from disk so shuangpin changes apply now.
    virtual bool reloadPinyinConfig();
    /// Invoke addon sub-config action for a specific input method engine.
    virtual bool invokeInputMethodSubConfig(const std::string &uniqueName,
                                            const std::string &subPath);
};

std::unique_ptr<ImeEngine> makeStubImeEngine();

} // namespace fcitx
