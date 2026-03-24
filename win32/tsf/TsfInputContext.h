#pragma once

#include <fcitx/event.h>
#include <fcitx/inputcontext.h>

namespace fcitx {

class Fcitx5ImeEngine;

/// Minimal InputContext for in-process TSF: commits go to the IME engine queue.
class TsfInputContext final : public InputContext {
  public:
    TsfInputContext(Fcitx5ImeEngine *engine, InputContextManager &mgr);
    ~TsfInputContext() override;

    const char *frontend() const override;

    void commitStringImpl(const std::string &text) override;
    void forwardKeyImpl(const ForwardKeyEvent & /*key*/) override {}
    void deleteSurroundingTextImpl(int /*offset*/, unsigned int /*size*/) override {}
    void updatePreeditImpl() override {}

  private:
    Fcitx5ImeEngine *engine_ = nullptr;
};

} // namespace fcitx
