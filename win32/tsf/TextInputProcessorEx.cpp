#include "tsf.h"

namespace fcitx {
namespace {
bool currentProcessIsExplorerForMinimalTray() {
    return currentProcessExeBaseNameEquals(L"explorer.exe");
}

bool currentProcessIsStandaloneTrayHelperForMinimalTsf() {
    return currentProcessIsStandaloneTrayHelper();
}
} // namespace

STDAPI Tsf::Activate(ITfThreadMgr *pThreadMgr, TfClientId tfClientId) {
    return ActivateEx(pThreadMgr, tfClientId, 0U);
}

STDAPI Tsf::Deactivate() {
    if (currentProcessIsStandaloneTrayHelperForMinimalTsf()) {
        initTextEditSink(nullptr);
        trayEditContextFallback_.Reset();
        threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;
        threadMgr_.Reset();
        clientId_ = TF_CLIENTID_NULL;
        tsfTrace("Deactivate helper minimal isolated");
        return S_OK;
    }
    if (currentProcessIsExplorerForMinimalTray()) {
        initTextEditSink(nullptr);
        trayEditContextFallback_.Reset();
        threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;
        threadMgr_.Reset();
        clientId_ = TF_CLIENTID_NULL;
        tsfTrace("Deactivate explorer minimal no-op");
        return S_OK;
    }
    candidateWin_.hide();
    resetCompositionState();
    resetShiftToggleGesture();
    uninitLangBarTrayItem();
    initTextEditSink(nullptr);
    trayEditContextFallback_.Reset();
    if (threadMgr_) {
        uninitThreadMgrEventSink();
        uninitKeyEventSink();
    }
    threadMgr_.Reset();
    clientId_ = TF_CLIENTID_NULL;
    return S_OK;
}

STDAPI Tsf::ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId,
                       DWORD dwFlags) {
    ComPtr<ITfDocumentMgr> documentMgr;
    threadMgr_ = pThreadMgr;
    clientId_ = tfClientId;
    if (currentProcessIsStandaloneTrayHelperForMinimalTsf()) {
        tsfTrace("ActivateEx helper minimal isolated");
        return S_OK;
    }
    if (currentProcessIsExplorerForMinimalTray()) {
        const bool helperReady = initShellTrayIcon();
        tsfTrace(std::string("ActivateEx explorer minimal helperReady=") +
                 (helperReady ? "true" : "false"));
        return S_OK;
    }
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
