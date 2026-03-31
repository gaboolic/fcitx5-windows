#include "ImeEngine.h"

namespace fcitx {

std::wstring ImeEngine::drainNextCommit() { return {}; }

bool ImeEngine::feedCandidatePick(size_t /*index*/) { return false; }

bool ImeEngine::tryForwardCandidateKey(unsigned /*vk*/) { return false; }

bool ImeEngine::tryForwardPreeditCommit() { return false; }

bool ImeEngine::imManagerHotkeyWouldEat(unsigned /*vk*/,
                                        std::uintptr_t /*lParam*/) const {
    return false;
}

bool ImeEngine::tryConsumeImManagerHotkey(unsigned /*vk*/,
                                          std::uintptr_t /*lParam*/) {
    return false;
}

bool ImeEngine::fcitxModifierHotkeyUsesFullKeyEvent(unsigned /*vk*/) const {
    return false;
}

bool ImeEngine::deliverFcitxRawKeyEvent(unsigned /*vk*/,
                                        std::uintptr_t /*lParam*/,
                                        bool /*isRelease*/) {
    return false;
}

std::vector<ProfileInputMethodItem> ImeEngine::profileInputMethods() const {
    return {};
}

bool ImeEngine::activateProfileInputMethod(const std::string & /*uniqueName*/) {
    return false;
}

std::string ImeEngine::currentInputMethod() const { return {}; }

std::vector<TrayStatusActionItem> ImeEngine::trayStatusActions() const {
    return {};
}

bool ImeEngine::activateTrayStatusAction(const std::string & /*uniqueName*/) {
    return false;
}

bool ImeEngine::reloadPinyinConfig() { return false; }

bool ImeEngine::invokeInputMethodSubConfig(const std::string & /*uniqueName*/,
                                           const std::string & /*subPath*/) {
    return false;
}

namespace {

class StubImeEngine : public ImeEngine {
  public:
    void clear() override {
        preedit_.clear();
        candidates_.clear();
        highlight_ = 0;
    }

    const std::wstring &preedit() const override { return preedit_; }
    const std::vector<std::wstring> &candidates() const override {
        return candidates_;
    }
    int highlightIndex() const override { return highlight_; }

    void setHighlightIndex(int index) override {
        if (index >= 0 && index < static_cast<int>(candidates_.size())) {
            highlight_ = index;
        }
    }

    void appendLatinLowercase(wchar_t ch) override {
        if (preedit_.size() >= 32) {
            return;
        }
        preedit_.push_back(ch);
        regenerateCandidates();
    }

    void backspace() override {
        if (!preedit_.empty()) {
            preedit_.pop_back();
            regenerateCandidates();
        }
    }

    void moveHighlight(int delta) override {
        const int n = static_cast<int>(candidates_.size());
        if (n <= 0) {
            return;
        }
        highlight_ = ((highlight_ + delta) % n + n) % n;
    }

    bool hasCandidate(size_t index) const override {
        return index < candidates_.size();
    }

    std::wstring candidateText(size_t index) const override {
        return hasCandidate(index) ? candidates_[index] : std::wstring();
    }

    std::wstring highlightedCandidateText() const override {
        if (highlight_ >= 0 &&
            highlight_ < static_cast<int>(candidates_.size())) {
            return candidates_[static_cast<size_t>(highlight_)];
        }
        return {};
    }

  private:
    void regenerateCandidates() {
        candidates_.clear();
        highlight_ = 0;
        if (preedit_.empty()) {
            return;
        }
        static const wchar_t *demo[] = {L"哈", L"蛤", L"铪", L"虾", L"吓"};
        for (const auto *s : demo) {
            candidates_.push_back(s);
        }
    }

    std::wstring preedit_;
    std::vector<std::wstring> candidates_;
    int highlight_ = 0;
};

} // namespace

std::unique_ptr<ImeEngine> makeStubImeEngine() {
    return std::make_unique<StubImeEngine>();
}

} // namespace fcitx
