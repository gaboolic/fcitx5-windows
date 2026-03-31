#include "tsf.h"

#include <Windows.h>

namespace {

/// Treat all Shift virtual keys as Shift (TSF may omit or vary scan code bits).
bool vkIsPhysicalShift(WPARAM vk, LPARAM lp) {
    (void)lp;
    return vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT;
}

bool chordCtrlOrAltDown() {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
           (GetKeyState(VK_MENU) & 0x8000) != 0;
}

/// True if the *other* Shift is still held (not the one releasing in this
/// message).
bool otherShiftStillHeld(WPARAM vk, LPARAM lp) {
    const unsigned sc = (static_cast<unsigned>(lp) >> 16) & 0xFFu;
    if (vk == VK_LSHIFT) {
        return (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
    }
    if (vk == VK_RSHIFT) {
        return (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
    }
    if (vk == static_cast<WPARAM>(VK_SHIFT)) {
        if (sc == 0x2Au) {
            return (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
        }
        if (sc == 0x36u) {
            return (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
        }
        // Unknown scan: do not block toggle (both-side check would rarely be
        // true).
        return false;
    }
    return false;
}

unsigned shiftScanCode(LPARAM lp) {
    return (static_cast<unsigned>(lp) >> 16) & 0xFFu;
}

} // namespace

namespace fcitx {

void Tsf::resetShiftToggleGesture() {
    shiftTapTrack_ = false;
    shiftTapInvalidated_ = false;
    shiftTapTogglePending_ = false;
    shiftTapTrackedWParam_ = 0;
    shiftTapTrackedScanCode_ = 0;
}

void Tsf::trackShiftToggleKeyDown(WPARAM wParam, LPARAM lParam) {
    if (vkIsPhysicalShift(wParam, lParam)) {
        if (chordCtrlOrAltDown()) {
            resetShiftToggleGesture();
            return;
        }
        const bool isRepeat =
            (static_cast<unsigned>(lParam) & 0x40000000u) != 0;
        if (isRepeat) {
            // Ignore keyboard auto-repeat on Shift so a single tap can still
            // toggle after the OS delivers repeat KeyDowns before KeyUp.
            return;
        }
        const unsigned sc = shiftScanCode(lParam);
        if (shiftTapTrack_) {
            if (shiftTapTrackedWParam_ == wParam &&
                shiftTapTrackedScanCode_ == sc) {
                // Some TSF hosts call both OnTestKeyDown and OnKeyDown for the
                // same physical Shift press; keep the tap gesture valid.
                return;
            }
            shiftTapInvalidated_ = true;
            return;
        }
        shiftTapTrack_ = true;
        shiftTapInvalidated_ = false;
        shiftTapTrackedWParam_ = wParam;
        shiftTapTrackedScanCode_ = sc;
        return;
    }
    // Lost Shift KeyUp (unrecognized vk/sc) would leave shiftTapTrack_ stuck
    // and break later gestures — clear when Shift is no longer held.
    if (shiftTapTrack_ && !(GetKeyState(VK_SHIFT) & 0x8000)) {
        resetShiftToggleGesture();
    } else if (shiftTapTrack_ && (GetKeyState(VK_SHIFT) & 0x8000)) {
        shiftTapInvalidated_ = true;
    }
}

void Tsf::trackShiftToggleKeyUp(WPARAM wParam, LPARAM lParam) {
    if (!vkIsPhysicalShift(wParam, lParam)) {
        return;
    }
    const bool doToggle = shiftTapTrack_ && !shiftTapInvalidated_ &&
                          !chordCtrlOrAltDown() &&
                          !otherShiftStillHeld(wParam, lParam);
    shiftTapTrack_ = false;
    shiftTapInvalidated_ = false;
    shiftTapTrackedWParam_ = 0;
    shiftTapTrackedScanCode_ = 0;
    shiftTapTogglePending_ = doToggle;
}
bool Tsf::initKeyEventSink() {
    ComPtr<ITfKeystrokeMgr> keystrokeMgr;
    if (FAILED(threadMgr_->QueryInterface(
            IID_ITfKeystrokeMgr, reinterpret_cast<void **>(
                                     keystrokeMgr.ReleaseAndGetAddressOf())))) {
        return false;
    }
    return keystrokeMgr->AdviseKeyEventSink(clientId_, (ITfKeyEventSink *)this,
                                            TRUE) == S_OK;
}

void Tsf::uninitKeyEventSink() {
    ComPtr<ITfKeystrokeMgr> keystrokeMgr;
    if (!threadMgr_) {
        return;
    }
    if (FAILED(threadMgr_->QueryInterface(
            IID_ITfKeystrokeMgr, reinterpret_cast<void **>(
                                     keystrokeMgr.ReleaseAndGetAddressOf())))) {
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
    pendingKeyHandled_ = false;
    HRESULT hr = E_FAIL;
    if (FAILED(textEditSinkContext_->RequestEditSession(
            clientId_, this, TF_ES_SYNC | TF_ES_READWRITE, &hr))) {
        return FALSE;
    }
    return SUCCEEDED(hr) && pendingKeyHandled_ ? TRUE : FALSE;
}

BOOL Tsf::processKeyUp(WPARAM wParam, LPARAM lParam) {
    if (!textEditSinkContext_ || !keyUpWouldBeHandled(wParam, lParam)) {
        return FALSE;
    }
    pendingKeyIsRelease_ = true;
    pendingKeyWParam_ = wParam;
    pendingKeyLParam_ = lParam;
    pendingKeyHandled_ = false;
    HRESULT hr = E_FAIL;
    if (FAILED(textEditSinkContext_->RequestEditSession(
            clientId_, this, TF_ES_SYNC | TF_ES_READWRITE, &hr))) {
        return FALSE;
    }
    return SUCCEEDED(hr) && pendingKeyHandled_ ? TRUE : FALSE;
}

bool Tsf::canProcessKeyDown(WPARAM wParam, LPARAM lParam) {
    return textEditSinkContext_ != nullptr && engine_ != nullptr &&
           keyWouldBeHandled(wParam, lParam);
}

bool Tsf::canProcessKeyUp(WPARAM wParam, LPARAM lParam) const {
    return textEditSinkContext_ != nullptr && engine_ != nullptr &&
           keyUpWouldBeHandled(wParam, lParam);
}

STDMETHODIMP Tsf::OnSetFocus(BOOL fForeground) { return S_OK; }

STDMETHODIMP Tsf::OnTestKeyDown(ITfContext *pContext, WPARAM wParam,
                                LPARAM lParam, BOOL *pfEaten) {
    trackShiftToggleKeyDown(wParam, lParam);
    if (sharedTrayChineseModeRequestPending() ||
        sharedTrayInputMethodRequestPending() ||
        sharedTrayStatusActionRequestPending() ||
        sharedTrayPinyinReloadRequestPending()) {
        tsfTrace("OnTestKeyDown shared tray request pending");
        scheduleSharedTrayChineseModeRequest(pContext);
        scheduleSharedTrayInputMethodRequest(pContext);
        scheduleSharedTrayStatusActionRequest(pContext);
        scheduleSharedTrayPinyinReloadRequest(pContext);
        if (sharedTrayChineseModeRequestPending() ||
            sharedTrayInputMethodRequestPending() ||
            sharedTrayStatusActionRequestPending() ||
            sharedTrayPinyinReloadRequestPending()) {
            *pfEaten = TRUE;
            return S_OK;
        }
    }
    // Peek only: must not run edit session here. Running processKey in both
    // OnTestKeyDown and OnKeyDown doubles input on some hosts (nii -> ni).
    *pfEaten = canProcessKeyDown(wParam, lParam) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP Tsf::OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                            BOOL *pfEaten) {
    (void)pContext;
    trackShiftToggleKeyDown(wParam, lParam);
    tsfTrace("OnKeyDown enter");
    scheduleSharedTrayChineseModeRequest(pContext);
    scheduleSharedTrayInputMethodRequest(pContext);
    scheduleSharedTrayStatusActionRequest(pContext);
    scheduleSharedTrayPinyinReloadRequest(pContext);
    if (!canProcessKeyDown(wParam, lParam)) {
        tsfTrace("OnKeyDown not handled by IME");
        *pfEaten = FALSE;
        return S_OK;
    }
    *pfEaten = processKey(wParam, lParam);
    tsfTrace(std::string("OnKeyDown processKey result=") +
             (*pfEaten ? "true" : "false"));
    return S_OK;
}

STDMETHODIMP Tsf::OnTestKeyUp(ITfContext *pContext, WPARAM wParam,
                              LPARAM lParam, BOOL *pfEaten) {
    trackShiftToggleKeyUp(wParam, lParam);
    if (sharedTrayChineseModeRequestPending() ||
        sharedTrayInputMethodRequestPending() ||
        sharedTrayStatusActionRequestPending() ||
        sharedTrayPinyinReloadRequestPending()) {
        scheduleSharedTrayChineseModeRequest(pContext);
        scheduleSharedTrayInputMethodRequest(pContext);
        scheduleSharedTrayStatusActionRequest(pContext);
        scheduleSharedTrayPinyinReloadRequest(pContext);
        if (sharedTrayChineseModeRequestPending() ||
            sharedTrayInputMethodRequestPending() ||
            sharedTrayStatusActionRequestPending() ||
            sharedTrayPinyinReloadRequestPending()) {
            *pfEaten = TRUE;
            return S_OK;
        }
    }
    *pfEaten = (shiftTapTogglePending_ || canProcessKeyUp(wParam, lParam))
                   ? TRUE
                   : FALSE;
    return S_OK;
}

STDMETHODIMP Tsf::OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                          BOOL *pfEaten) {
    (void)pContext;
    trackShiftToggleKeyUp(wParam, lParam);
    if (shiftTapTogglePending_) {
        shiftTapTogglePending_ = false;
        if (engine_) {
            langBarScheduleToggleChinese();
        }
        *pfEaten = TRUE;
        return S_OK;
    }
    if (!canProcessKeyUp(wParam, lParam)) {
        *pfEaten = FALSE;
        return S_OK;
    }
    *pfEaten = processKeyUp(wParam, lParam);
    return S_OK;
}

STDMETHODIMP Tsf::OnPreservedKey(ITfContext *pContext, REFGUID rguid,
                                 BOOL *pfEaten) {
    return S_OK;
}
} // namespace fcitx
