#include "tsf.h"
#include "LangBarTray.h"
#include "../dll/util.h"

namespace fcitx {
namespace {
bool currentProcessIsExplorerForMinimalTray() {
    return currentProcessIsShellInputHost();
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
        const HRESULT hrCreate = CoCreateInstance(
            CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
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
        tsfTrace(
            "initActiveLanguageProfileNotifySink missing threadMgr ITfSource");
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
    if (!threadMgr_ ||
        activeLanguageProfileNotifySinkCookie_ == TF_INVALID_COOKIE) {
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
    if (dwProfileType != TF_PROFILETYPE_INPUTPROCESSOR ||
        !IsEqualGUID(clsid, FCITX_CLSID)) {
        return S_OK;
    }
    const bool active = (dwFlags & TF_IPSINK_FLAG_ACTIVE) != 0;
    tsfTrace(std::string("OnActivated(profile) active=") +
             (active ? "true" : "false") + " flags=0x" +
             std::to_string(static_cast<unsigned long>(dwFlags)));
    handleProfileActivated(active);
    return S_OK;
}

STDAPI Tsf::OnActivated(REFCLSID clsid, REFGUID, BOOL fActivated) {
    if (!IsEqualGUID(clsid, FCITX_CLSID)) {
        return S_OK;
    }
    tsfTrace(std::string("OnActivated(active-profile) active=") +
             (fActivated ? "true" : "false"));
    handleProfileActivated(fActivated != FALSE);
    return S_OK;
}

STDAPI Tsf::Deactivate() {
    if (currentProcessIsExplorerForMinimalTray()) {
        initTextEditSink(nullptr);
        trayEditContextFallback_.Reset();
        if (!destroying_) {
            tsfTrace("Deactivate explorer minimal keepalive");
            return S_OK;
        }
        uninitLangBarTrayItem();
        uninitCompartmentEventSinks();
        uninitActiveLanguageProfileNotifySink();
        uninitProfileActivationSink();
        threadMgr_.Reset();
        clientId_ = TF_CLIENTID_NULL;
        tsfTrace("Deactivate explorer minimal final cleanup");
        return S_OK;
    }
    flushSharedTrayScheduleFromFocusIfPending();
    sharedTrayFocusScheduleTick_ = 0;
    sharedTrayFocusSchedulePending_ = false;
    candidateWin_.hide();
    resetCompositionState();
    resetShiftToggleGesture();
    uninitLangBarTrayItem();
    initTextEditSink(nullptr);
    trayEditContextFallback_.Reset();
    if (threadMgr_) {
        uninitCompartmentEventSinks();
        uninitActiveLanguageProfileNotifySink();
        uninitThreadMgrEventSink();
        uninitKeyEventSink();
    }
    uninitProfileActivationSink();
    threadMgr_.Reset();
    clientId_ = TF_CLIENTID_NULL;
    return S_OK;
}

STDAPI Tsf::ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId,
                       DWORD dwFlags) {
    ComPtr<ITfDocumentMgr> documentMgr;
    (void)dwFlags;
    threadMgr_ = pThreadMgr;
    clientId_ = tfClientId;
    if (currentProcessIsExplorerForMinimalTray()) {
        if (!initProfileActivationSink()) {
            tsfTrace("ActivateEx explorer minimal initProfileActivationSink "
                     "failed");
        }
        if (!initActiveLanguageProfileNotifySink()) {
            tsfTrace("ActivateEx explorer minimal "
                     "initActiveLanguageProfileNotifySink failed");
        }
        if (!initCompartmentEventSinks()) {
            tsfTrace("ActivateEx explorer minimal initCompartmentEventSinks "
                     "failed");
        }
        initLangBarTrayItem();
        if (langBarItem_) {
            langBarItem_->Show(TRUE);
        }
        syncKeyboardOpenCompartment(true);
        langBarNotifyIconUpdate();
        tsfTrace("ActivateEx explorer minimal attempted TSF lang-bar "
                 "registration");
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

    if (!initProfileActivationSink()) {
        tsfTrace("ActivateEx initProfileActivationSink failed");
    }
    if (!initActiveLanguageProfileNotifySink()) {
        tsfTrace("ActivateEx initActiveLanguageProfileNotifySink failed");
    }
    if (!initCompartmentEventSinks()) {
        tsfTrace("ActivateEx initCompartmentEventSinks failed");
    }
    initLangBarTrayItem();
    if (langBarItem_) {
        langBarItem_->Show(TRUE);
    }
    syncKeyboardOpenCompartment(true);
    langBarNotifyIconUpdate();

    return S_OK;

ActivateExError:
    Deactivate();
    return E_FAIL;
}
} // namespace fcitx
