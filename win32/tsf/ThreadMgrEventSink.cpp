#include "tsf.h"

namespace fcitx {
namespace {
bool currentProcessIsExplorerForThreadMgr() {
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

bool Tsf::initThreadMgrEventSink() {
    ComPtr<ITfSource> source;
    if (FAILED(threadMgr_->QueryInterface(
            IID_ITfSource,
            reinterpret_cast<void **>(source.ReleaseAndGetAddressOf())))) {
        return false;
    }
    if (source->AdviseSink(IID_ITfThreadMgrEventSink,
                           (ITfThreadMgrEventSink *)this,
                           &threadMgrEventSinkCookie_) != S_OK) {
        threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;
    }
    return threadMgrEventSinkCookie_ != TF_INVALID_COOKIE;
}

void Tsf::uninitThreadMgrEventSink() {
    ComPtr<ITfSource> source;
    if (!threadMgr_ || threadMgrEventSinkCookie_ == TF_INVALID_COOKIE) {
        return;
    }
    if (SUCCEEDED(threadMgr_->QueryInterface(
            IID_ITfSource,
            reinterpret_cast<void **>(source.ReleaseAndGetAddressOf())))) {
        source->UnadviseSink(threadMgrEventSinkCookie_);
    }
    threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;
}

STDMETHODIMP Tsf::OnInitDocumentMgr(ITfDocumentMgr *pDocMgr) { return S_OK; }

STDMETHODIMP Tsf::OnUninitDocumentMgr(ITfDocumentMgr *pDocMgr) { return S_OK; }

STDMETHODIMP Tsf::OnSetFocus(ITfDocumentMgr *pDocMgrFocus,
                             ITfDocumentMgr *pDocMgrPrevFocus) {
    tsfTrace("OnSetFocus document manager focus changed");
    initTextEditSink(pDocMgrFocus);
    if (currentProcessIsExplorerForThreadMgr() && !shellTrayHostHwnd_) {
        const bool trayReady = initShellTrayIcon();
        tsfTrace(std::string("OnSetFocus recreated explorer tray host=") +
                 (trayReady ? "true" : "false"));
    }
    deferredSharedTrayInputMethod_.clear();
    scheduleSharedTrayChineseModeRequest();
    scheduleSharedTrayStatusActionRequest();
    return S_OK;
}

STDMETHODIMP Tsf::OnPushContext(ITfContext *pContext) { return S_OK; }

STDMETHODIMP Tsf::OnPopContext(ITfContext *pContext) { return S_OK; }
} // namespace fcitx
