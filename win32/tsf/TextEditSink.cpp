#include "tsf.h"

namespace fcitx {
bool Tsf::initTextEditSink(ITfDocumentMgr *documentMgr) {
    ComPtr<ITfSource> source;
    // clear out any previous sink first
    if (textEditSinkCookie_ != TF_INVALID_COOKIE) {
        if (SUCCEEDED(textEditSinkContext_->QueryInterface(
                IID_ITfSource, reinterpret_cast<void **>(source.ReleaseAndGetAddressOf())))) {
            source->UnadviseSink(textEditSinkCookie_);
        }
        textEditSinkContext_.Reset();
        textEditSinkCookie_ = TF_INVALID_COOKIE;
    }
    if (documentMgr == nullptr) {
        return true; // caller just wanted to clear the previous sink
    }
    if (FAILED(documentMgr->GetTop(textEditSinkContext_.ReleaseAndGetAddressOf()))) {
        return false;
    }
    if (!textEditSinkContext_) {
        return true; // empty document, no sink possible
    }
    source.Reset();
    bool ret = false;
    if (SUCCEEDED(textEditSinkContext_->QueryInterface(
            IID_ITfSource, reinterpret_cast<void **>(source.ReleaseAndGetAddressOf())))) {
        if (SUCCEEDED(source->AdviseSink(IID_ITfTextEditSink, (ITfTextEditSink *)this,
                                         &textEditSinkCookie_))) {
            ret = true;
        } else {
            textEditSinkCookie_ = TF_INVALID_COOKIE;
        }
    }
    if (!ret) {
        textEditSinkContext_.Reset();
    }
    return ret;
}

STDMETHODIMP Tsf::OnEndEdit(ITfContext *pic, TfEditCookie ecReadOnly,
                            ITfEditRecord *pEditRecord) {
    return S_OK;
}
} // namespace fcitx
