#include "TsfInputContext.h"
#include "Fcitx5ImeEngine.h"
#include "tsf.h"

namespace fcitx {

TsfInputContext::TsfInputContext(Fcitx5ImeEngine *engine,
                                 InputContextManager &mgr)
    : InputContext(mgr, "tsf"), engine_(engine) {
    created();
}

TsfInputContext::~TsfInputContext() { destroy(); }

const char *TsfInputContext::frontend() const { return "tsf"; }

void TsfInputContext::commitStringImpl(const std::string &text) {
    tsfTrace("commitStringImpl text=" + text);
    if (engine_) {
        engine_->enqueueCommitUtf8(text);
    }
}

} // namespace fcitx
