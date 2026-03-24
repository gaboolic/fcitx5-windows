#pragma once

// MinGW/WIDL msctf.h is incomplete vs the Windows SDK. Fill gaps needed by the TSF IME.

#if defined(__MINGW32__)

#ifndef TF_CLIENTID_NULL
#define TF_CLIENTID_NULL ((TfClientId)0)
#endif
#ifndef TF_INVALID_UIELEMENTID
#define TF_INVALID_UIELEMENTID ((DWORD)-1)
#endif
#ifndef TF_CLUIE_DOCUMENTMGR
#define TF_CLUIE_DOCUMENTMGR 0x00000001
#define TF_CLUIE_COUNT 0x00000002
#define TF_CLUIE_SELECTION 0x00000004
#define TF_CLUIE_STRING 0x00000008
#define TF_CLUIE_PAGEINDEX 0x00000010
#define TF_CLUIE_CURRENTPAGE 0x00000020
#endif

#endif /* __MINGW32__ */

#if defined(__MINGW32__) && !defined(__ITfTextInputProcessorEx_INTERFACE_DEFINED__)
#define __ITfTextInputProcessorEx_INTERFACE_DEFINED__

EXTERN_C const IID IID_ITfTextInputProcessorEx;

#if defined(__cplusplus) && !defined(CINTERFACE)
MIDL_INTERFACE("6e4e2102-f9cd-433d-b496-303ce03a6507")
ITfTextInputProcessorEx : public ITfTextInputProcessor {
  public:
    virtual HRESULT STDMETHODCALLTYPE
    ActivateEx(ITfThreadMgr *ptim, TfClientId tid, DWORD dwFlags) = 0;
};
#endif /* C++ */

#endif /* ITfTextInputProcessorEx */

#if defined(__MINGW32__) && !defined(__ITfCandidateListUIElement_INTERFACE_DEFINED__)
#define __ITfCandidateListUIElement_INTERFACE_DEFINED__

EXTERN_C const IID IID_ITfCandidateListUIElement;

#if defined(__cplusplus) && !defined(CINTERFACE)
MIDL_INTERFACE("ea1ea138-19df-11d7-a6d2-00065b84435c")
ITfCandidateListUIElement : public ITfUIElement {
  public:
    virtual HRESULT STDMETHODCALLTYPE GetUpdatedFlags(DWORD *pdwFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr **ppdim) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT *puCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSelection(UINT *puIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetString(UINT uIndex, BSTR *pstr) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPageIndex(UINT *pIndex, UINT uSize,
                                                   UINT *puPageCnt) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPageIndex(UINT *pIndex, UINT uPageCnt) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentPage(UINT *puPage) = 0;
};
#endif /* C++ */

#endif /* ITfCandidateListUIElement */
