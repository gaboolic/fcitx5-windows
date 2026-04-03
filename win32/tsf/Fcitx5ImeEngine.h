#pragma once

#include "ImeEngine.h"

#include <fcitx-utils/key.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace fcitx {

class Instance;
class TsfInputContext;
class Fcitx5ImePipeShared;

/// Try to create an engine backed by in-process `fcitx::Instance` + dynamic
/// addons. Returns nullptr if initialization fails (caller should use stub).
std::unique_ptr<ImeEngine> makeFcitx5ImeEngineAttempt();

class Fcitx5ImeEngine : public ImeEngine {
  public:
    Fcitx5ImeEngine();
    ~Fcitx5ImeEngine() override;

    bool init();

    /// Pipe server: share one `Instance` from \p host; own a dedicated
    /// `TsfInputContext`. Does not create an `Instance`.
    bool initAsPipeSession(Fcitx5ImePipeShared *host);

    /// Pump libuv a few times (in-process TSF and pipe server use this).
    void pumpEventLoopForUi() override;

    void enqueueCommitUtf8(std::string text);

    void clear() override;
    void syncInputPanelFromIme() override;
    const std::wstring &preedit() const override;
    int preeditCaretUtf16() const override;
    const std::vector<std::wstring> &candidates() const override;
    int highlightIndex() const override;
    void setHighlightIndex(int index) override;

    bool appendLatinLowercase(wchar_t ch) override;
    bool backspace() override;
    void moveHighlight(int delta) override;

    bool hasCandidate(size_t index) const override;
    std::wstring candidateText(size_t index) const override;
    std::wstring highlightedCandidateText() const override;

    std::wstring drainNextCommit() override;
    bool feedCandidatePick(size_t index) override;
    bool tryForwardCandidateKey(unsigned vk) override;
    bool tryForwardPreeditCommit() override;

    bool imManagerHotkeyWouldEat(unsigned vk,
                                 std::uintptr_t lParam) const override;
    bool tryConsumeImManagerHotkey(unsigned vk, std::uintptr_t lParam) override;

    bool fcitxModifierHotkeyUsesFullKeyEvent(unsigned vk) const override;
    bool deliverFcitxRawKeyEvent(unsigned vk, std::uintptr_t lParam,
                                 bool isRelease,
                                 std::uint32_t hostKeyboardStateMask) override;
    std::vector<ProfileInputMethodItem> profileInputMethods() const override;
    bool activateProfileInputMethod(const std::string &uniqueName) override;
    std::string currentInputMethod() const override;
    std::vector<TrayStatusActionItem> trayStatusActions() const override;
    bool activateTrayStatusAction(const std::string &uniqueName) override;
    bool reloadPinyinConfig() override;
    bool reloadRimeAddonConfig() override;
    bool invokeInputMethodSubConfig(const std::string &uniqueName,
                                    const std::string &subPath) override;

  private:
    bool initWithInputMethod(const std::string &preferredInputMethod);
    bool rebuildForInputMethod(const std::string &preferredInputMethod);
    bool sendKeySym(KeySym sym);
    void syncUiFromIc();
    /// If the loaded profile has no IM entries (or missing DefaultIM), TSF
    /// cannot call setCurrentInputMethod for pinyin — repair in-memory and
    /// persist so new installs work without manually copying profile.example.
    void ensurePortableImGroupHasEntries();
    /// @param localIm If true (in-proc TSF), apply as per-IC local IM. If false
    /// (pipe server session), follow global profile so every app’s context
    /// tracks tray / defaultInputMethod instead of sticking on pinyin.
    void activatePreferredInputMethod(const std::string &preferredInputMethod = {},
                                      bool localIm = true);
    /// Pipe session: same refresh/activate sequence as the original in-proc
    /// init (single pass; avoid repeated activate/save confusing fcitx state).
    void activatePreferredInputMethodPipeSync(
        Instance *inst, const std::string &preferredInputMethod);

    Instance *instancePtr() const;

    bool loggingAttached_ = false;

    /// When non-null, `instance_` is unused; `Instance` comes from the pipe host.
    Fcitx5ImePipeShared *pipeSharedHost_ = nullptr;
    std::unique_ptr<Instance> instance_;
    std::unique_ptr<TsfInputContext> ic_;

    std::deque<std::string> commitQueueUtf8_;

    std::wstring preeditWide_;
    std::vector<std::wstring> candidatesWide_;
    int highlightIndex_ = 0;
    int preeditCaretWide_ = 0;
};

} // namespace fcitx
