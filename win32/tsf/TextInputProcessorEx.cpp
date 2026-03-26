#include "tsf.h"

namespace fcitx {
namespace {
bool currentProcessIsExplorerForMinimalTray() {
    WCHAR exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        return false;
    }
    const std::wstring_view path(exePath);
    const size_t pos = path.find_last_of(L"\\/");
    const std::wstring_view file =
        pos == std::wstring_view::npos ? path : path.substr(pos + 1);
    return _wcsicmp(std::wstring(file).c_str(), L"explorer.exe") == 0;
}
} // namespace

STDAPI Tsf::Activate(ITfThreadMgr *pThreadMgr, TfClientId tfClientId) {
    return ActivateEx(pThreadMgr, tfClientId, 0U);
}

STDAPI Tsf::Deactivate() {
    candidateWin_.hide();
    resetCompositionState();
    resetShiftToggleGesture();
    uninitLangBarTrayItem();
    if (currentProcessIsExplorerForMinimalTray()) {
        initTextEditSink(nullptr);
        trayEditContextFallback_.Reset();
        threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;
        threadMgr_.Reset();
        clientId_ = TF_CLIENTID_NULL;
        return S_OK;
    }
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
    if (currentProcessIsExplorerForMinimalTray()) {
        const bool trayReady = initLangBarTrayItem();
        tsfTrace(std::string("ActivateEx explorer minimal trayReady=") +
                 (trayReady ? "true" : "false"));
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
