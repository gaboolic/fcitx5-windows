#include "tsf.h"

namespace fcitx {
STDMETHODIMP Tsf::OnCompositionTerminated(TfEditCookie /*ecWrite*/,
                                          ITfComposition * /*pComposition*/) {
    composition_.Reset();
    compositionRange_.Reset();
    if (engine_) {
        engine_->clear();
    }
    candidateWin_.hide();
    endCandidateListUiElement();
    return S_OK;
}
} // namespace fcitx
