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
    deferredSharedTrayInputMethod_.clear();
    // Throttle shared-tray sync from document-mgr focus storms
    // (Explorer/desktop switches can deliver many OnSetFocus calls). Key path
    // still runs above; tray sync is eventual. Deactivate() flushes if a burst
    // ended while pending.
    constexpr ULONGLONG kSharedTrayFocusThrottleMs = 80;
    const ULONGLONG now = GetTickCount64();
    if (sharedTrayFocusScheduleTick_ == 0ULL ||
        now - sharedTrayFocusScheduleTick_ >= kSharedTrayFocusThrottleMs) {
        sharedTrayFocusScheduleTick_ = now;
        sharedTrayFocusSchedulePending_ = false;
        scheduleSharedTrayChineseModeRequest();
        scheduleSharedTrayStatusActionRequest();
    } else {
        sharedTrayFocusSchedulePending_ = true;
    }
    return S_OK;
}

void Tsf::flushSharedTrayScheduleFromFocusIfPending() {
    if (!sharedTrayFocusSchedulePending_) {
        return;
    }
    sharedTrayFocusSchedulePending_ = false;
    sharedTrayFocusScheduleTick_ = GetTickCount64();
    scheduleSharedTrayChineseModeRequest();
    scheduleSharedTrayStatusActionRequest();
}

STDMETHODIMP Tsf::OnPushContext(ITfContext *pContext) { return S_OK; }

STDMETHODIMP Tsf::OnPopContext(ITfContext *pContext) { return S_OK; }
} // namespace fcitx
