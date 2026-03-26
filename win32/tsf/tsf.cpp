#include "tsf.h"
#include <cassert>
#include <string_view>

#if FCITX_WIN32_IME_WITH_CORE
#include "Fcitx5ImeEngine.h"
#endif

extern void DllAddRef();
extern void DllRelease();

namespace fcitx {

namespace {

LONG gExplorerProcessLifetimeDllPinned = 0;

bool currentProcessIsExplorerForDefaultEngine() {
    WCHAR exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        return false;
    }
    const std::wstring_view path(exePath);
    const size_t pos = path.find_last_of(L"\\/");
    const std::wstring_view file =
        pos == std::wstring_view::npos ? path : path.substr(pos + 1);
    return _wcsicmp(std::wstring(file).c_str(), L"explorer.exe") == 0;
}

std::unique_ptr<ImeEngine> makeDefaultImeEngine() {
#if FCITX_WIN32_IME_WITH_CORE
    if (!currentProcessIsExplorerForDefaultEngine()) {
        if (auto e = makeFcitx5ImeEngineAttempt()) {
            return e;
        }
    }
#endif
    return makeStubImeEngine();
}

} // namespace

Tsf::Tsf(std::unique_ptr<ImeEngine> engine)
    : engine_(engine ? std::move(engine) : makeDefaultImeEngine()) {
    DllAddRef();
    if (currentProcessIsExplorerForDefaultEngine() &&
        InterlockedCompareExchange(&gExplorerProcessLifetimeDllPinned, 1, 0) ==
            0) {
        DllAddRef();
        tsfTrace("Tsf::Tsf pinned DLL for explorer process lifetime");
    }
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

Tsf::~Tsf() {
    if (threadMgr_ || langBarItem_ || shellTrayHostHwnd_ || textEditSinkContext_ ||
        trayEditContextFallback_) {
        Deactivate();
    }
    DllRelease();
}

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
