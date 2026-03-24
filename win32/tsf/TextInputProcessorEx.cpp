#include "tsf.h"

namespace fcitx {
STDAPI Tsf::Activate(ITfThreadMgr *pThreadMgr, TfClientId tfClientId) {
    return ActivateEx(pThreadMgr, tfClientId, 0U);
}

STDAPI Tsf::Deactivate() {
    candidateWin_.hide();
    resetCompositionState();
    resetShiftToggleGesture();
    uninitLangBarTrayItem();
    initTextEditSink(nullptr);
    trayEditContextFallback_.Reset();
    uninitThreadMgrEventSink();
    uninitKeyEventSink();
    threadMgr_.Reset();
    clientId_ = TF_CLIENTID_NULL;
    return S_OK;
}

STDAPI Tsf::ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId,
                       DWORD dwFlags) {
    ComPtr<ITfDocumentMgr> documentMgr;
    threadMgr_ = pThreadMgr;
    clientId_ = tfClientId;
    if (!initThreadMgrEventSink()) {
        goto ActivateExError;
    }

    if ((threadMgr_->GetFocus(documentMgr.ReleaseAndGetAddressOf()) == S_OK) &&
        documentMgr) {
        initTextEditSink(documentMgr.Get());
    }

    if (!initKeyEventSink()) {
        goto ActivateExError;
    }

    initLangBarTrayItem();

    return S_OK;

ActivateExError:
    Deactivate();
    return E_FAIL;
}
} // namespace fcitx
