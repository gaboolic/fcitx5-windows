#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fcitx {

// Pluggable input logic (pinyin → preedit/candidates). TSF layer only displays
// and commits strings; replace Stub with fcitx5/libime-backed engine later.
class ImeEngine {
  public:
    virtual ~ImeEngine() = default;

    virtual void clear() = 0;

    virtual const std::wstring &preedit() const = 0;
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

    /// Commits produced by fcitx core (`InputContext::commitString`) between TSF
    /// edit-session steps. Default: none (stub engine).
    virtual std::wstring drainNextCommit();

    /// If true, mouse pick is handled by the engine (fcitx digit keys). If false,
    /// TSF commits `candidateText(index)` (stub).
    virtual bool feedCandidatePick(size_t index);

    /// Forward Up/Down/Space/Return/digit to fcitx when the candidate window is up.
    virtual bool tryForwardCandidateKey(unsigned vk);
    /// Forward Return to fcitx when there is preedit but no candidate list.
    virtual bool tryForwardPreeditCommit();

    /// Keys bound in global `profile` (enumerate IM / enumerate group). Stub: never.
    virtual bool imManagerHotkeyWouldEat(unsigned vk, std::uintptr_t lParam) const;
    /// Apply hotkey if it matches `GlobalConfig`; returns true if the key was consumed.
    virtual bool tryConsumeImManagerHotkey(unsigned vk, std::uintptr_t lParam);
};

std::unique_ptr<ImeEngine> makeStubImeEngine();

} // namespace fcitx
