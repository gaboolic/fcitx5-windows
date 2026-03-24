#pragma once

#include "ImeEngine.h"

#include <fcitx-utils/key.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace fcitx {

class Instance;
class EventDispatcher;
class TsfInputContext;

/// Try to create an engine backed by in-process `fcitx::Instance` + dynamic addons.
/// Returns nullptr if initialization fails (caller should use stub).
std::unique_ptr<ImeEngine> makeFcitx5ImeEngineAttempt();

class Fcitx5ImeEngine : public ImeEngine {
  public:
    Fcitx5ImeEngine();
    ~Fcitx5ImeEngine() override;

    bool init();

    void enqueueCommitUtf8(std::string text);

    void clear() override;
    const std::wstring &preedit() const override;
    const std::vector<std::wstring> &candidates() const override;
    int highlightIndex() const override;
    void setHighlightIndex(int index) override;

    void appendLatinLowercase(wchar_t ch) override;
    void backspace() override;
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
    bool tryConsumeImManagerHotkey(unsigned vk,
                                   std::uintptr_t lParam) override;

  private:
    bool sendKeySym(KeySym sym);
    void syncUiFromIc();
    void activatePreferredInputMethod();

    bool loggingAttached_ = false;

    std::unique_ptr<Instance> instance_;
    std::unique_ptr<EventDispatcher> dispatcher_;
    std::unique_ptr<TsfInputContext> ic_;

    std::deque<std::string> commitQueueUtf8_;

    std::wstring preeditWide_;
    std::vector<std::wstring> candidatesWide_;
    int highlightIndex_ = 0;
};

} // namespace fcitx
