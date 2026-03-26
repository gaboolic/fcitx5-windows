#include "tsf.h"
#include "TrayServiceIpc.h"
#include "../dll/util.h"

namespace fcitx {
namespace {
bool currentProcessIsExplorerForMinimalTray() {
    return currentProcessExeBaseNameEquals(L"explorer.exe");
}

bool currentProcessIsStandaloneTrayHelperForMinimalTsf() {
    return currentProcessIsStandaloneTrayHelper();
}
} // namespace

bool queryActiveFcitxTipForExplorer(bool *active) {
    if (!active) {
        return false;
    }
    *active = false;
    ComPtr<ITfInputProcessorProfileMgr> mgr;
    const HRESULT hrCreate =
        CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                         CLSCTX_INPROC_SERVER,
                         IID_ITfInputProcessorProfileMgr,
                         reinterpret_cast<void **>(mgr.GetAddressOf()));
    if (FAILED(hrCreate) || !mgr) {
        tsfTrace("queryActiveFcitxTipForExplorer CoCreateInstance failed hr=0x" +
                 std::to_string(static_cast<unsigned long>(hrCreate)));
        return false;
    }
    TF_INPUTPROCESSORPROFILE profile = {};
    const HRESULT hrProfile =
        mgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile);
    if (FAILED(hrProfile)) {
        tsfTrace("queryActiveFcitxTipForExplorer GetActiveProfile failed hr=0x" +
                 std::to_string(static_cast<unsigned long>(hrProfile)));
        return false;
    }
    *active = profile.dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR &&
              IsEqualGUID(profile.clsid, FCITX_CLSID);
    return true;
}

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
    const bool visible =
        (dwFlags & TF_IPSINK_FLAG_ACTIVE) &&
        dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR &&
        IsEqualGUID(clsid, FCITX_CLSID);
    pushTrayServiceExplorerRefreshHint(visible, kTrayServiceExplorerRefreshDelayMs);
    tsfTrace(std::string("OnActivated explorer minimal visible=") +
             (visible ? "true" : "false") + " flags=0x" +
             std::to_string(static_cast<unsigned long>(dwFlags)));
    return S_OK;
}

STDAPI Tsf::OnActivated(REFCLSID clsid, REFGUID, BOOL fActivated) {
    if (!currentProcessIsExplorerForMinimalTray()) {
        return S_OK;
    }
    const bool visible = fActivated && IsEqualGUID(clsid, FCITX_CLSID);
    pushTrayServiceExplorerRefreshHint(visible, kTrayServiceExplorerRefreshDelayMs);
    tsfTrace(std::string("OnActivated(active-profile) explorer minimal visible=") +
             (visible ? "true" : "false"));
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
        pushTrayServiceExplorerRefreshHint(false, kTrayServiceExplorerRefreshDelayMs);
        initTextEditSink(nullptr);
        trayEditContextFallback_.Reset();
        uninitProfileActivationSink();
        uninitActiveLanguageProfileNotifySink();
        uninitThreadMgrEventSink();
        threadMgr_.Reset();
        clientId_ = TF_CLIENTID_NULL;
        tsfTrace("Deactivate explorer minimal no-op visible=false");
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
        const bool threadMgrSinkReady = initThreadMgrEventSink();
        const bool profileSinkReady = initProfileActivationSink();
        const bool activeProfileSinkReady =
            profileSinkReady ? true : initActiveLanguageProfileNotifySink();
        bool active = false;
        if (queryActiveFcitxTipForExplorer(&active)) {
            pushTrayServiceExplorerRefreshHint(
                active, kTrayServiceExplorerRefreshDelayMs);
        }
        tsfTrace(std::string("ActivateEx explorer minimal helperReady=") +
                 (helperReady ? "true" : "false") +
                 " threadMgrSinkReady=" +
                 (threadMgrSinkReady ? "true" : "false") +
                 " profileSinkReady=" + (profileSinkReady ? "true" : "false") +
                 " activeProfileSinkReady=" +
                 (activeProfileSinkReady ? "true" : "false"));
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
    langBarNotifyIconUpdate();

    return S_OK;

ActivateExError:
    Deactivate();
    return E_FAIL;
}
} // namespace fcitx
