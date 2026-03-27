#include "CandidateListUiElement.h"
#include "tsf.h"

#include <oleauto.h>

namespace fcitx {

CandidateListUiElement::CandidateListUiElement(Tsf *owner) : owner_(owner) {}

STDMETHODIMP CandidateListUiElement::QueryInterface(REFIID riid,
                                                    void **ppvObject) {
    if (ppvObject == nullptr) {
        return E_INVALIDARG;
    }
    *ppvObject = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfUIElement)) {
        *ppvObject = static_cast<ITfUIElement *>(this);
    } else if (IsEqualIID(riid, IID_ITfCandidateListUIElement)) {
        *ppvObject = static_cast<ITfCandidateListUIElement *>(this);
    }
    if (*ppvObject) {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CandidateListUiElement::AddRef() { return ++refCount_; }

STDMETHODIMP_(ULONG) CandidateListUiElement::Release() {
    const LONG n = --refCount_;
    if (n == 0) {
        delete this;
    }
    return static_cast<ULONG>(n);
}

STDMETHODIMP CandidateListUiElement::GetDescription(BSTR *pbstrDescription) {
    if (!pbstrDescription) {
        return E_INVALIDARG;
    }
    *pbstrDescription = SysAllocString(L"Fcitx5 candidate list");
    return *pbstrDescription ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateListUiElement::GetGUID(GUID *pguid) {
    if (!pguid) {
        return E_INVALIDARG;
    }
    *pguid = IID_ITfCandidateListUIElement;
    return S_OK;
}

STDMETHODIMP CandidateListUiElement::Show(BOOL bShow) {
    uiShown_ = bShow;
    return S_OK;
}

STDMETHODIMP CandidateListUiElement::IsShown(BOOL *pbShow) {
    if (!pbShow) {
        return E_INVALIDARG;
    }
    *pbShow = uiShown_;
    return S_OK;
}

STDMETHODIMP CandidateListUiElement::GetUpdatedFlags(DWORD *pdwFlags) {
    if (!pdwFlags) {
        return E_INVALIDARG;
    }
    *pdwFlags = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION |
                TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
    return S_OK;
}

STDMETHODIMP CandidateListUiElement::GetDocumentMgr(ITfDocumentMgr **ppdim) {
    if (!ppdim) {
        return E_INVALIDARG;
    }
    *ppdim = nullptr;
    if (!owner_) {
        return E_FAIL;
    }
    return owner_->queryCandidateListDocumentMgr(ppdim);
}

STDMETHODIMP CandidateListUiElement::GetCount(UINT *puCount) {
    if (!puCount || !owner_ || !owner_->engine_) {
        return E_INVALIDARG;
    }
    const auto &c = owner_->engine_->candidates();
    *puCount = static_cast<UINT>(c.size());
    return S_OK;
}

STDMETHODIMP CandidateListUiElement::GetSelection(UINT *puIndex) {
    if (!puIndex || !owner_ || !owner_->engine_) {
        return E_INVALIDARG;
    }
    const int hi = owner_->engine_->highlightIndex();
    *puIndex = hi < 0 ? 0 : static_cast<UINT>(hi);
    return S_OK;
}

STDMETHODIMP CandidateListUiElement::GetString(UINT uIndex, BSTR *pstr) {
    if (!pstr || !owner_ || !owner_->engine_) {
        return E_INVALIDARG;
    }
    *pstr = nullptr;
    if (!owner_->engine_->hasCandidate(static_cast<size_t>(uIndex))) {
        return E_INVALIDARG;
    }
    const std::wstring s = owner_->engine_->candidateText(uIndex);
    *pstr = SysAllocStringLen(s.c_str(), static_cast<UINT>(s.size()));
    return *pstr ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateListUiElement::GetPageIndex(UINT *pIndex, UINT uSize,
                                                  UINT *puPageCnt) {
    if (!pIndex || !puPageCnt || !owner_ || !owner_->engine_) {
        return E_INVALIDARG;
    }
    const UINT n = static_cast<UINT>(owner_->engine_->candidates().size());
    if (uSize < n) {
        return E_INVALIDARG;
    }
    for (UINT i = 0; i < n; ++i) {
        pIndex[i] = 0;
    }
    *puPageCnt = 1;
    return S_OK;
}

STDMETHODIMP CandidateListUiElement::SetPageIndex(UINT * /*pIndex*/,
                                                  UINT /*uPageCnt*/) {
    return S_OK;
}

STDMETHODIMP CandidateListUiElement::GetCurrentPage(UINT *puPage) {
    if (!puPage) {
        return E_INVALIDARG;
    }
    *puPage = 0;
    return S_OK;
}

} // namespace fcitx
