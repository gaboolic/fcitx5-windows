#include "../tsf/tsf.h"
#include "register.h"

CRITICAL_SECTION CS;
LONG dllRefCount = 0;

void DllAddRef() { InterlockedIncrement(&dllRefCount); }

void DllRelease() { InterlockedDecrement(&dllRefCount); }

class NoopTextInputProcessor final : public ITfTextInputProcessorEx {
  public:
    NoopTextInputProcessor() { DllAddRef(); }

    ~NoopTextInputProcessor() { DllRelease(); }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override {
        if (ppvObject == nullptr) {
            return E_INVALIDARG;
        }
        *ppvObject = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_ITfTextInputProcessor) ||
            IsEqualIID(riid, IID_ITfTextInputProcessorEx)) {
            *ppvObject = static_cast<ITfTextInputProcessorEx *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return ++refCount_; }

    STDMETHODIMP_(ULONG) Release() override {
        const LONG ret = --refCount_;
        if (ret == 0) {
            delete this;
        }
        return ret;
    }

    STDMETHODIMP Activate(ITfThreadMgr *, TfClientId) override { return S_OK; }

    STDMETHODIMP Deactivate() override { return S_OK; }

    STDMETHODIMP ActivateEx(ITfThreadMgr *, TfClientId, DWORD) override {
        return S_OK;
    }

  private:
    LONG refCount_ = 1;
};

class ClassFactory : public IClassFactory {
  public:
    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override {
        if (IsEqualIID(riid, IID_IClassFactory) ||
            IsEqualIID(riid, IID_IUnknown)) {
            *ppvObject = this;
            DllAddRef();
            return NOERROR;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        DllAddRef();
        return dllRefCount;
    }

    STDMETHODIMP_(ULONG) Release() override {
        DllRelease();
        return dllRefCount;
    }

    // IClassFactory methods
    STDMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid,
                                void **ppvObject) override {
        if (ppvObject == nullptr)
            return E_INVALIDARG;
        *ppvObject = nullptr;
        if (pUnkOuter != nullptr)
            return CLASS_E_NOAGGREGATION;
        if (fcitx::currentProcessIsStandaloneTrayHelper()) {
            auto *noop = new NoopTextInputProcessor();
            if (noop == nullptr) {
                return E_OUTOFMEMORY;
            }
            fcitx::tsfTrace(
                "ClassFactory::CreateInstance returning helper noop TIP");
            const HRESULT hr = noop->QueryInterface(riid, ppvObject);
            noop->Release();
            return hr;
        }
        auto *tsf = new fcitx::Tsf();
        if (tsf == nullptr)
            return E_OUTOFMEMORY;
        const HRESULT hr = tsf->QueryInterface(riid, ppvObject);
        tsf->Release(); // caller still holds ref if hr == S_OK
        return hr;
    }

    STDMETHODIMP LockServer(BOOL fLock) override {
        fLock ? DllAddRef() : DllRelease();
        return S_OK;
    }
};

ClassFactory *factory = nullptr;

#pragma warning(push)
#pragma warning(disable : 4502 4518)

__declspec(dllexport) STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid,
                                               void **ppvObject) {
    if (factory == nullptr) {
        EnterCriticalSection(&CS);
        if (factory == nullptr) {
            factory = new ClassFactory();
        }
        LeaveCriticalSection(&CS);
    }
    if (IsEqualIID(riid, IID_IClassFactory) || IsEqualIID(riid, IID_IUnknown)) {
        *ppvObject = factory;
        DllAddRef();
        return NOERROR;
    }
    *ppvObject = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

__declspec(dllexport) STDAPI DllCanUnloadNow() { return dllRefCount == 0; }

__declspec(dllexport) STDAPI DllUnregisterServer() {
    fcitx::UnregisterCategoriesAndProfiles();
    fcitx::UnregisterServer();
    return S_OK;
}

__declspec(dllexport) STDAPI DllRegisterServer() {
    if (fcitx::RegisterServer() && fcitx::RegisterProfiles() &&
        fcitx::RegisterCategories()) {
        return S_OK;
    }
    DllUnregisterServer();
    return E_FAIL;
}

#pragma warning(pop)

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID pvReserved) {
    switch (dwReason) {
    case DLL_PROCESS_ATTACH:
        fcitx::dllInstance = hInstance;
        if (!InitializeCriticalSectionAndSpinCount(&CS, 0)) {
            return FALSE;
        }
        break;
    case DLL_PROCESS_DETACH:
        DeleteCriticalSection(&CS);
        break;
    }
    return TRUE;
}
