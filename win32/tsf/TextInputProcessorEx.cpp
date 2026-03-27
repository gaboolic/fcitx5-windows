#include "tsf.h"

#include <atomic>

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

bool Tsf::initProfileActivationSink() {
    if (profileActivationSinkCookie_ != TF_INVALID_COOKIE) {
        return true;
    }
    if (!profileMgr_) {
        const HRESULT hrCreate =
            CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                             CLSCTX_INPROC_SERVER,
                             IID_ITfInputProcessorProfileMgr,
                             reinterpret_cast<void **>(profileMgr_.GetAddressOf()));
        if (FAILED(hrCreate) || !profileMgr_) {
            tsfTrace("initProfileActivationSink CoCreateInstance failed hr=0x" +
                     std::to_string(static_cast<unsigned long>(hrCreate)));
            profileMgr_.Reset();
            return false;
        }
    }
    ComPtr<ITfSource> source;
    if (FAILED(profileMgr_.As(&source)) || !source) {
        tsfTrace("initProfileActivationSink missing profile ITfSource");
        return false;
    }
    const HRESULT hr = source->AdviseSink(
        IID_ITfInputProcessorProfileActivationSink,
        static_cast<ITfInputProcessorProfileActivationSink *>(this),
        &profileActivationSinkCookie_);
    if (FAILED(hr)) {
        tsfTrace("initProfileActivationSink AdviseSink failed hr=0x" +
                 std::to_string(static_cast<unsigned long>(hr)));
        profileActivationSinkCookie_ = TF_INVALID_COOKIE;
        return false;
    }
    return true;
}

bool Tsf::initActiveLanguageProfileNotifySink() {
    if (activeLanguageProfileNotifySinkCookie_ != TF_INVALID_COOKIE) {
        return true;
    }
    if (!threadMgr_) {
        tsfTrace("initActiveLanguageProfileNotifySink missing threadMgr");
        return false;
    }
    ComPtr<ITfSource> source;
    if (FAILED(threadMgr_.As(&source)) || !source) {
        tsfTrace("initActiveLanguageProfileNotifySink missing threadMgr ITfSource");
        return false;
    }
    const HRESULT hr = source->AdviseSink(
        IID_ITfActiveLanguageProfileNotifySink,
        static_cast<ITfActiveLanguageProfileNotifySink *>(this),
        &activeLanguageProfileNotifySinkCookie_);
    if (FAILED(hr)) {
        tsfTrace("initActiveLanguageProfileNotifySink AdviseSink failed hr=0x" +
                 std::to_string(static_cast<unsigned long>(hr)));
        activeLanguageProfileNotifySinkCookie_ = TF_INVALID_COOKIE;
        return false;
    }
    return true;
}

void Tsf::uninitProfileActivationSink() {
    if (!profileMgr_ || profileActivationSinkCookie_ == TF_INVALID_COOKIE) {
        profileActivationSinkCookie_ = TF_INVALID_COOKIE;
        return;
    }
    ComPtr<ITfSource> source;
    if (SUCCEEDED(profileMgr_.As(&source)) && source) {
        source->UnadviseSink(profileActivationSinkCookie_);
    }
    profileActivationSinkCookie_ = TF_INVALID_COOKIE;
    profileMgr_.Reset();
}

void Tsf::uninitActiveLanguageProfileNotifySink() {
    if (!threadMgr_ || activeLanguageProfileNotifySinkCookie_ == TF_INVALID_COOKIE) {
        activeLanguageProfileNotifySinkCookie_ = TF_INVALID_COOKIE;
        return;
    }
    ComPtr<ITfSource> source;
    if (SUCCEEDED(threadMgr_.As(&source)) && source) {
        source->UnadviseSink(activeLanguageProfileNotifySinkCookie_);
    }
    activeLanguageProfileNotifySinkCookie_ = TF_INVALID_COOKIE;
}

STDAPI Tsf::OnActivated(DWORD dwProfileType, LANGID, REFCLSID clsid, REFGUID,
                        REFGUID, HKL, DWORD dwFlags) {
    if (!currentProcessIsExplorerForMinimalTray()) {
        return S_OK;
    }
    (void)dwProfileType;
    (void)clsid;
    tsfTrace(std::string("OnActivated explorer minimal ignored flags=0x") +
             std::to_string(static_cast<unsigned long>(dwFlags)));
    return S_OK;
}

STDAPI Tsf::OnActivated(REFCLSID clsid, REFGUID, BOOL fActivated) {
    if (!currentProcessIsExplorerForMinimalTray()) {
        return S_OK;
    }
    (void)clsid;
    (void)fActivated;
    tsfTrace("OnActivated(active-profile) explorer minimal ignored");
    return S_OK;
}

STDAPI Tsf::Deactivate() {
    if (currentProcessIsStandaloneTrayHelperForMinimalTsf()) {
        initTextEditSink(nullptr);
        trayEditContextFallback_.Reset();
        profileMgr_.Reset();
        activeLanguageProfileNotifySinkCookie_ = TF_INVALID_COOKIE;
        profileActivationSinkCookie_ = TF_INVALID_COOKIE;
        threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;
        threadMgr_.Reset();
        clientId_ = TF_CLIENTID_NULL;
        tsfTrace("Deactivate helper minimal isolated");
        return S_OK;
    }
    if (currentProcessIsExplorerForMinimalTray()) {
        initTextEditSink(nullptr);
        trayEditContextFallback_.Reset();
        threadMgr_.Reset();
        clientId_ = TF_CLIENTID_NULL;
        tsfTrace("Deactivate explorer minimal bootstrap-only");
        return S_OK;
    }
    flushSharedTrayScheduleFromFocusIfPending();
    sharedTrayFocusScheduleTick_ = 0;
    sharedTrayFocusSchedulePending_ = false;
    pushTrayServiceTipSessionEvent(false);
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
    (void)dwFlags;
    if (currentProcessIsStandaloneTrayHelperForMinimalTsf()) {
        tsfTrace("ActivateEx helper minimal isolated (no threadMgr bind)");
        return S_OK;
    }
    if (currentProcessIsExplorerForMinimalTray()) {
        // MSCTF + explorer.exe: ActivateEx/Deactivate can fire in bursts when the
        // foreground moves to the shell/desktop. Only touch the tray helper until
        // the first successful ensure — repeated FindWindow / launch during focus
        // churn stresses Explorer + the notification area (see Weasel-style: avoid
        // redundant work on the shell TIP path).
        static std::atomic<bool> s_explorerTrayHelperPrimed{false};
        if (!s_explorerTrayHelperPrimed.load(std::memory_order_relaxed)) {
            const bool helperReady = initShellTrayIcon();
            if (helperReady) {
                s_explorerTrayHelperPrimed.store(true, std::memory_order_relaxed);
            }
            tsfTrace(std::string("ActivateEx explorer minimal helperReady=") +
                     (helperReady ? "true" : "false") +
                     " ensure-helper-only no threadMgr");
        } else {
            tsfTrace("ActivateEx explorer minimal skipped (helper already primed)");
        }
        return S_OK;
    }
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
    langBarNotifyIconUpdate();
    pushTrayServiceTipSessionEvent(true);

    return S_OK;

ActivateExError:
    Deactivate();
    return E_FAIL;
}
} // namespace fcitx
