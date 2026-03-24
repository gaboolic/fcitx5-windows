#include "tsf.h"

namespace fcitx {
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
    if (threadMgrEventSinkCookie_ == TF_INVALID_COOKIE) {
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
    initTextEditSink(pDocMgrFocus);
    return S_OK;
}

STDMETHODIMP Tsf::OnPushContext(ITfContext *pContext) { return S_OK; }

STDMETHODIMP Tsf::OnPopContext(ITfContext *pContext) { return S_OK; }
} // namespace fcitx
