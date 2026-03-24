#include "tsf.h"

namespace fcitx {
bool Tsf::initKeyEventSink() {
    ComPtr<ITfKeystrokeMgr> keystrokeMgr;
    if (FAILED(threadMgr_->QueryInterface(
            IID_ITfKeystrokeMgr,
            reinterpret_cast<void **>(keystrokeMgr.ReleaseAndGetAddressOf())))) {
        return false;
    }
    return keystrokeMgr->AdviseKeyEventSink(clientId_, (ITfKeyEventSink *)this,
                                            TRUE) == S_OK;
}

void Tsf::uninitKeyEventSink() {
    ComPtr<ITfKeystrokeMgr> keystrokeMgr;
    if (FAILED(threadMgr_->QueryInterface(
            IID_ITfKeystrokeMgr,
            reinterpret_cast<void **>(keystrokeMgr.ReleaseAndGetAddressOf())))) {
        return;
    }
    keystrokeMgr->UnadviseKeyEventSink(clientId_);
}

BOOL Tsf::processKey(WPARAM wParam, LPARAM lParam) {
    if (!textEditSinkContext_ || !keyWouldBeHandled(wParam, lParam)) {
        return FALSE;
    }
    pendingKeyIsRelease_ = false;
    pendingKeyWParam_ = wParam;
    pendingKeyLParam_ = lParam;
    HRESULT hr = E_FAIL;
    if (FAILED(textEditSinkContext_->RequestEditSession(
            clientId_, this, TF_ES_SYNC | TF_ES_READWRITE, &hr))) {
        return FALSE;
    }
    return SUCCEEDED(hr) ? TRUE : FALSE;
}

BOOL Tsf::processKeyUp(WPARAM wParam, LPARAM lParam) {
    if (!textEditSinkContext_ || !keyUpWouldBeHandled(wParam, lParam)) {
        return FALSE;
    }
    pendingKeyIsRelease_ = true;
    pendingKeyWParam_ = wParam;
    pendingKeyLParam_ = lParam;
    HRESULT hr = E_FAIL;
    if (FAILED(textEditSinkContext_->RequestEditSession(
            clientId_, this, TF_ES_SYNC | TF_ES_READWRITE, &hr))) {
        return FALSE;
    }
    return SUCCEEDED(hr) ? TRUE : FALSE;
}

STDMETHODIMP Tsf::OnSetFocus(BOOL fForeground) { return S_OK; }

STDMETHODIMP Tsf::OnTestKeyDown(ITfContext *pContext, WPARAM wParam,
                                LPARAM lParam, BOOL *pfEaten) {
    if (keyDownHandled_) {
        *pfEaten = TRUE;
    } else {
        *pfEaten = keyDownHandled_ = processKey(wParam, lParam);
    }
    return S_OK;
}

STDMETHODIMP Tsf::OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                            BOOL *pfEaten) {
    if (keyDownHandled_) {
        keyDownHandled_ = FALSE;
        *pfEaten = TRUE;
    } else {
        *pfEaten = keyDownHandled_ = processKey(wParam, lParam);
    }
    return S_OK;
}

STDMETHODIMP Tsf::OnTestKeyUp(ITfContext *pContext, WPARAM wParam,
                              LPARAM lParam, BOOL *pfEaten) {
    if (keyUpHandled_) {
        *pfEaten = TRUE;
    } else {
        *pfEaten = keyUpHandled_ = processKeyUp(wParam, lParam);
    }
    return S_OK;
}

STDMETHODIMP Tsf::OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                          BOOL *pfEaten) {
    if (keyUpHandled_) {
        keyUpHandled_ = FALSE;
        *pfEaten = TRUE;
    } else {
        *pfEaten = keyUpHandled_ = processKeyUp(wParam, lParam);
    }
    return S_OK;
}

STDMETHODIMP Tsf::OnPreservedKey(ITfContext *pContext, REFGUID rguid,
                                 BOOL *pfEaten) {
    return S_OK;
}
} // namespace fcitx
