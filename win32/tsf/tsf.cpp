#include "tsf.h"
#include <cassert>

#if FCITX_WIN32_IME_WITH_CORE
#include "Fcitx5ImeEngine.h"
#endif

extern void DllAddRef();
extern void DllRelease();

namespace fcitx {

namespace {

std::unique_ptr<ImeEngine> makeDefaultImeEngine() {
#if FCITX_WIN32_IME_WITH_CORE
    if (auto e = makeFcitx5ImeEngineAttempt()) {
        return e;
    }
#endif
    return makeStubImeEngine();
}

} // namespace

Tsf::Tsf(std::unique_ptr<ImeEngine> engine)
    : engine_(engine ? std::move(engine) : makeDefaultImeEngine()) {
    DllAddRef();
    candidateWin_.setOnPick([this](int idx) {
        pendingMousePick_ = idx;
        if (!textEditSinkContext_) {
            return;
        }
        HRESULT hr = E_FAIL;
        textEditSinkContext_->RequestEditSession(
            clientId_, this, TF_ES_SYNC | TF_ES_READWRITE, &hr);
    });
}

Tsf::~Tsf() { DllRelease(); }

// Windows also queries ITfDisplayAttributeCollectionProvider
// {3977526D-1A0A-435A-8D06-ECC9516B484F} which is internal and we simply
// ignore.
STDAPI Tsf::QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr) {
        return E_INVALIDARG;
    }
    *ppvObject = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor))
        *ppvObject = (ITfTextInputProcessor *)this;
    else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx))
        *ppvObject = (ITfTextInputProcessorEx *)this;
    else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
        *ppvObject = (ITfThreadMgrEventSink *)this;
    else if (IsEqualIID(riid, IID_ITfTextEditSink))
        *ppvObject = (ITfTextEditSink *)this;
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
        *ppvObject = (ITfKeyEventSink *)this;
    else if (IsEqualIID(riid, IID_ITfEditSession))
        *ppvObject = (ITfEditSession *)this;
    else if (IsEqualIID(riid, IID_ITfCompositionSink))
        *ppvObject = (ITfCompositionSink *)this;

    if (*ppvObject) {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDAPI_(ULONG) Tsf::AddRef() { return ++refCount_; }

STDAPI_(ULONG) Tsf::Release() {
    LONG ret = --refCount_;
    assert(refCount_ >= 0);
    if (refCount_ == 0) {
        delete this;
    }
    return ret;
}
} // namespace fcitx
