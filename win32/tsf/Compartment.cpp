#include "tsf.h"
#include "LangBarTray.h"

namespace fcitx {
namespace {

bool adviseCompartmentSink(ITfThreadMgr *threadMgr, REFGUID guid,
                           IUnknown *sink, ComPtr<ITfCompartment> *compartment,
                           DWORD *cookie) {
    if (!threadMgr || !sink || !compartment || !cookie) {
        return false;
    }
    ComPtr<ITfCompartmentMgr> compartmentMgr;
    if (FAILED(threadMgr->QueryInterface(
            IID_ITfCompartmentMgr,
            reinterpret_cast<void **>(
                compartmentMgr.ReleaseAndGetAddressOf()))) ||
        !compartmentMgr) {
        return false;
    }
    if (FAILED(compartmentMgr->GetCompartment(guid,
                                              compartment->GetAddressOf())) ||
        !*compartment) {
        compartment->Reset();
        return false;
    }
    ComPtr<ITfSource> source;
    if (FAILED(compartment->As(&source)) || !source) {
        compartment->Reset();
        return false;
    }
    const HRESULT hr =
        source->AdviseSink(IID_ITfCompartmentEventSink, sink, cookie);
    if (FAILED(hr)) {
        compartment->Reset();
        *cookie = TF_INVALID_COOKIE;
        return false;
    }
    return true;
}

void unadviseCompartmentSink(ComPtr<ITfCompartment> *compartment,
                             DWORD *cookie) {
    if (!compartment || !cookie || *cookie == TF_INVALID_COOKIE ||
        !*compartment) {
        if (cookie) {
            *cookie = TF_INVALID_COOKIE;
        }
        if (compartment) {
            compartment->Reset();
        }
        return;
    }
    ComPtr<ITfSource> source;
    if (SUCCEEDED(compartment->As(&source)) && source) {
        source->UnadviseSink(*cookie);
    }
    *cookie = TF_INVALID_COOKIE;
    compartment->Reset();
}

} // namespace

bool Tsf::initCompartmentEventSinks() {
    bool keyboardOk = keyboardOpenCompartmentSinkCookie_ != TF_INVALID_COOKIE;
    bool inputModeOk =
        inputModeConversionCompartmentSinkCookie_ != TF_INVALID_COOKIE;
    if (!keyboardOk) {
        keyboardOk = adviseCompartmentSink(
            threadMgr_.Get(), GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
            static_cast<ITfCompartmentEventSink *>(this),
            &keyboardOpenCompartment_, &keyboardOpenCompartmentSinkCookie_);
        if (!keyboardOk) {
            tsfTrace("initCompartmentEventSinks failed keyboard-open sink");
        }
    }
    if (!inputModeOk) {
        inputModeOk = adviseCompartmentSink(
            threadMgr_.Get(), GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION,
            static_cast<ITfCompartmentEventSink *>(this),
            &inputModeConversionCompartment_,
            &inputModeConversionCompartmentSinkCookie_);
        if (!inputModeOk) {
            tsfTrace("initCompartmentEventSinks failed input-mode sink");
        }
    }
    return keyboardOk || inputModeOk;
}

void Tsf::uninitCompartmentEventSinks() {
    unadviseCompartmentSink(&keyboardOpenCompartment_,
                            &keyboardOpenCompartmentSinkCookie_);
    unadviseCompartmentSink(&inputModeConversionCompartment_,
                            &inputModeConversionCompartmentSinkCookie_);
    suppressKeyboardOpenCompartmentChange_ = false;
    suppressInputModeConversionCompartmentChange_ = false;
}

bool Tsf::readThreadCompartmentDword(REFGUID guid, DWORD *value) const {
    if (!threadMgr_ || !value) {
        return false;
    }
    ComPtr<ITfCompartmentMgr> compartmentMgr;
    if (FAILED(threadMgr_->QueryInterface(
            IID_ITfCompartmentMgr,
            reinterpret_cast<void **>(
                compartmentMgr.ReleaseAndGetAddressOf()))) ||
        !compartmentMgr) {
        return false;
    }
    ComPtr<ITfCompartment> compartment;
    if (FAILED(
            compartmentMgr->GetCompartment(guid, compartment.GetAddressOf())) ||
        !compartment) {
        return false;
    }
    VARIANT var;
    VariantInit(&var);
    const HRESULT hr = compartment->GetValue(&var);
    if (FAILED(hr) || var.vt != VT_I4) {
        VariantClear(&var);
        return false;
    }
    *value = static_cast<DWORD>(var.lVal);
    VariantClear(&var);
    return true;
}

bool Tsf::writeThreadCompartmentDword(REFGUID guid, DWORD value) const {
    if (!threadMgr_ || clientId_ == TF_CLIENTID_NULL) {
        tsfTrace(
            "writeThreadCompartmentDword skipped missing threadMgr/clientId");
        return false;
    }
    ComPtr<ITfCompartmentMgr> compartmentMgr;
    if (FAILED(threadMgr_->QueryInterface(
            IID_ITfCompartmentMgr,
            reinterpret_cast<void **>(
                compartmentMgr.ReleaseAndGetAddressOf()))) ||
        !compartmentMgr) {
        return false;
    }
    ComPtr<ITfCompartment> compartment;
    if (FAILED(
            compartmentMgr->GetCompartment(guid, compartment.GetAddressOf())) ||
        !compartment) {
        return false;
    }
    VARIANT var;
    VariantInit(&var);
    var.vt = VT_I4;
    var.lVal = static_cast<LONG>(value);
    const HRESULT hr = compartment->SetValue(clientId_, &var);
    VariantClear(&var);
    tsfTrace("writeThreadCompartmentDword hr=0x" +
             std::to_string(static_cast<unsigned long>(hr)) + " value=0x" +
             std::to_string(static_cast<unsigned long>(value)));
    return SUCCEEDED(hr);
}

void Tsf::syncKeyboardOpenCompartment(bool forceOpen) {
    DWORD current = 0;
    if (!forceOpen && !readThreadCompartmentDword(
                          GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &current)) {
        return;
    }
    const DWORD desired = 1;
    if (current == desired) {
        tsfTrace("syncKeyboardOpenCompartment already open");
        return;
    }
    tsfTrace("syncKeyboardOpenCompartment forceOpen=" +
             std::string(forceOpen ? "true" : "false") + " current=0x" +
             std::to_string(static_cast<unsigned long>(current)) +
             " desired=0x" +
             std::to_string(static_cast<unsigned long>(desired)));
    suppressKeyboardOpenCompartmentChange_ = true;
    writeThreadCompartmentDword(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, desired);
    suppressKeyboardOpenCompartmentChange_ = false;
}

void Tsf::syncInputModeConversionCompartment(bool forceWrite) {
    DWORD current = 0;
    const bool haveCurrent = readThreadCompartmentDword(
        GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &current);
    DWORD desired =
        haveCurrent
            ? (current & static_cast<DWORD>(TF_CONVERSIONMODE_FULLSHAPE))
            : 0;
    if (chineseActive_) {
        desired |= TF_CONVERSIONMODE_NATIVE;
    }
    if (!forceWrite && haveCurrent && desired == current) {
        tsfTrace("syncInputModeConversionCompartment unchanged current=0x" +
                 std::to_string(static_cast<unsigned long>(current)));
        return;
    }
    tsfTrace(
        "syncInputModeConversionCompartment forceWrite=" +
        std::string(forceWrite ? "true" : "false") +
        " chinese=" + std::string(chineseActive_ ? "true" : "false") +
        " current=0x" + std::to_string(static_cast<unsigned long>(current)) +
        " desired=0x" + std::to_string(static_cast<unsigned long>(desired)));
    suppressInputModeConversionCompartmentChange_ = true;
    writeThreadCompartmentDword(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION,
                                desired);
    suppressInputModeConversionCompartmentChange_ = false;
}

void Tsf::handleProfileActivated(bool active) {
    if (active && !langBarItem_) {
        initLangBarTrayItem();
    }
    if (active && langBarItem_) {
        langBarItem_->Show(TRUE);
    }
    if (active) {
        syncKeyboardOpenCompartment(true);
        langBarNotifyIconUpdate();
    } else {
        langBarNotifyIconUpdate();
    }
}

STDMETHODIMP Tsf::OnChange(REFGUID rguid) {
    if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
        tsfTrace("OnChange keyboard-open compartment");
        if (suppressKeyboardOpenCompartmentChange_) {
            return S_OK;
        }
        DWORD open = 0;
        if (readThreadCompartmentDword(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                                       &open) &&
            open == 0) {
            syncKeyboardOpenCompartment(true);
        }
        if (langBarItem_) {
            langBarItem_->Show(TRUE);
        }
        langBarNotifyIconUpdate();
        return S_OK;
    }
    if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION)) {
        tsfTrace("OnChange inputmode compartment");
        if (suppressInputModeConversionCompartmentChange_) {
            return S_OK;
        }
        DWORD flags = 0;
        if (readThreadCompartmentDword(
                GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &flags)) {
            const bool wantChinese = (flags & TF_CONVERSIONMODE_NATIVE) != 0;
            if (wantChinese != chineseActive_) {
                langBarScheduleSetChineseMode(wantChinese);
            } else {
                langBarNotifyIconUpdate();
            }
        }
        return S_OK;
    }
    return S_OK;
}

} // namespace fcitx
