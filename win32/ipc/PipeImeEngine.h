#pragma once

#include "Fcitx5ImeIpcProtocol.h"
#include "ImeEngine.h"

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace fcitx {

struct ImeIpcDecoded;

std::unique_ptr<ImeEngine> makePipeImeEngineAttempt();

class PipeImeEngine final : public ImeEngine {
  public:
    PipeImeEngine();
    ~PipeImeEngine() override;

    bool pingConnect();

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
    bool tryConsumeImManagerHotkey(unsigned vk,
                                   std::uintptr_t lParam) override;

    bool fcitxModifierHotkeyUsesFullKeyEvent(unsigned vk) const override;
    bool deliverFcitxRawKeyEvent(unsigned vk, std::uintptr_t lParam,
                                 bool isRelease,
                                 std::uint32_t hostKeyboardStateMask) override;

    bool usesHostKeyboardStateForRawKeyDelivery() const override;

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
    static void appendU32(std::vector<std::uint8_t> &b, std::uint32_t v);
    static void appendU64(std::vector<std::uint8_t> &b, std::uint64_t v);
    static void appendUtf8(std::vector<std::uint8_t> &b,
                           const std::string &s);

    bool ensurePipeConnectedUnlocked() const;
    bool tryLaunchPipeServerProcess() const;
    void closePipeUnlocked() const;
    bool transact(ImeIpcOpcode op,
                  const std::vector<std::uint8_t> &body) const;
    void applySnapshot(const ImeIpcDecoded &d) const;

    mutable std::mutex mutex_;
    mutable HANDLE pipe_ = INVALID_HANDLE_VALUE;

    mutable std::wstring preedit_;
    mutable std::vector<std::wstring> candidates_;
    mutable int highlight_ = 0;
    mutable int caretUtf16_ = 0;
    mutable std::string currentImUtf8_;
    mutable std::vector<ProfileInputMethodItem> profileIms_;
    mutable std::vector<TrayStatusActionItem> trayActions_;
    mutable std::uint32_t lastFlags_ = 0;
    mutable std::string lastDrainedCommitUtf8_;
};

} // namespace fcitx
