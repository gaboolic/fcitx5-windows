#pragma once

#include "MsctfMingwCompat.h"
#include <msctf.h>

// MinGW WIDL msctf declares virtual AdviseSink on ITfLangBarItemButton; Windows
// SDK + Clang often does not, so `override` is invalid there (-Werror).
#if defined(__MINGW32__) || defined(__MSYS__) || defined(__CYGWIN__)
#define FCITX_TSF_LBI_SINK_OVERRIDE override
#else
#define FCITX_TSF_LBI_SINK_OVERRIDE
#endif

namespace fcitx {

class Tsf;

class FcitxLangBarButton final : public ITfLangBarItemButton, public ITfSource {
  public:
    explicit FcitxLangBarButton(Tsf *tsf);
    ~FcitxLangBarButton();

    void notifyModeChanged();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO *pInfo) override;
    STDMETHODIMP GetStatus(DWORD *pdwStatus) override;
    STDMETHODIMP Show(BOOL fShow) override;
    STDMETHODIMP GetTooltipString(BSTR *pbstrToolTip) override;

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt,
                         const RECT *prcArea) override;
    STDMETHODIMP InitMenu(ITfMenu *pMenu) override;
    STDMETHODIMP OnMenuSelect(UINT wID) override;
    STDMETHODIMP GetIcon(HICON *phIcon) override;
    STDMETHODIMP GetText(BSTR *pbstrText) override;
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown *punk,
                            DWORD *pdwCookie) FCITX_TSF_LBI_SINK_OVERRIDE;
    STDMETHODIMP UnadviseSink(DWORD dwCookie) FCITX_TSF_LBI_SINK_OVERRIDE;

  private:
    static constexpr DWORD kSinkCookie = 0x50494e47;

    Tsf *tsf_;
    ULONG ref_;
    DWORD status_;
    ITfLangBarItemSink *sink_;
};

} // namespace fcitx
