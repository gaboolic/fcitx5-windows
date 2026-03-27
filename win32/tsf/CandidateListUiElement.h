#pragma once

#include "MsctfMingwCompat.h"
#include <Windows.h>
#include <msctf.h>

namespace fcitx {

class Tsf;

/// Minimal COM object exposing ITfCandidateListUIElement for ITfUIElementMgr
/// (accessibility / UI automation alongside the custom CandidateWindow).
class CandidateListUiElement final : public ITfCandidateListUIElement {
  public:
    explicit CandidateListUiElement(Tsf *owner);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfUIElement
    STDMETHODIMP GetDescription(BSTR *pbstrDescription) override;
    STDMETHODIMP GetGUID(GUID *pguid) override;
    STDMETHODIMP Show(BOOL bShow) override;
    STDMETHODIMP IsShown(BOOL *pbShow) override;

    // ITfCandidateListUIElement
    STDMETHODIMP GetUpdatedFlags(DWORD *pdwFlags) override;
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr **ppdim) override;
    STDMETHODIMP GetCount(UINT *puCount) override;
    STDMETHODIMP GetSelection(UINT *puIndex) override;
    STDMETHODIMP GetString(UINT uIndex, BSTR *pstr) override;
    STDMETHODIMP GetPageIndex(UINT *pIndex, UINT uSize,
                              UINT *puPageCnt) override;
    STDMETHODIMP SetPageIndex(UINT *pIndex, UINT uPageCnt) override;
    STDMETHODIMP GetCurrentPage(UINT *puPage) override;

  private:
    ~CandidateListUiElement() = default;

    LONG refCount_ = 1;
    Tsf *owner_ = nullptr;
    BOOL uiShown_ = TRUE;
};

} // namespace fcitx
