#pragma once

// Must come before any EXTERN_C / COM declarations below. (clang-format
// SortIncludes may reorder other headers to put this file first.)
#include <Windows.h>
#include <msctf.h>

// MinGW/WIDL msctf.h is incomplete vs the Windows SDK. Fill gaps needed by the
// TSF IME. MSYS2 /usr/bin/g++ defines __MSYS__ but not __MINGW32__.

#if defined(__MINGW32__) || defined(__MSYS__)

#ifndef TF_CLIENTID_NULL
#define TF_CLIENTID_NULL ((TfClientId)0)
#endif
#ifndef TF_INVALID_UIELEMENTID
#define TF_INVALID_UIELEMENTID ((DWORD) - 1)
#endif
#ifndef TF_CLUIE_DOCUMENTMGR
#define TF_CLUIE_DOCUMENTMGR 0x00000001
#define TF_CLUIE_COUNT 0x00000002
#define TF_CLUIE_SELECTION 0x00000004
#define TF_CLUIE_STRING 0x00000008
#define TF_CLUIE_PAGEINDEX 0x00000010
#define TF_CLUIE_CURRENTPAGE 0x00000020
#endif

#endif /* __MINGW32__ || __MSYS__ */

#if (defined(__MINGW32__) || defined(__MSYS__)) &&                             \
    !defined(__ITfTextInputProcessorEx_INTERFACE_DEFINED__)
#define __ITfTextInputProcessorEx_INTERFACE_DEFINED__

EXTERN_C const IID IID_ITfTextInputProcessorEx;

#if defined(__cplusplus) && !defined(CINTERFACE)
MIDL_INTERFACE("6e4e2102-f9cd-433d-b496-303ce03a6507")
ITfTextInputProcessorEx : public ITfTextInputProcessor {
  public:
    virtual HRESULT STDMETHODCALLTYPE ActivateEx(
        ITfThreadMgr * ptim, TfClientId tid, DWORD dwFlags) = 0;
};
#endif /* C++ */

#endif /* ITfTextInputProcessorEx */

#if (defined(__MINGW32__) || defined(__MSYS__)) &&                             \
    !defined(__ITfCandidateListUIElement_INTERFACE_DEFINED__)
#define __ITfCandidateListUIElement_INTERFACE_DEFINED__

EXTERN_C const IID IID_ITfCandidateListUIElement;

#if defined(__cplusplus) && !defined(CINTERFACE)
MIDL_INTERFACE("ea1ea138-19df-11d7-a6d2-00065b84435c")
ITfCandidateListUIElement : public ITfUIElement {
  public:
    virtual HRESULT STDMETHODCALLTYPE GetUpdatedFlags(DWORD * pdwFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr *
                                                     *ppdim) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT * puCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSelection(UINT * puIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetString(UINT uIndex, BSTR * pstr) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPageIndex(UINT * pIndex, UINT uSize,
                                                   UINT * puPageCnt) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPageIndex(UINT * pIndex,
                                                   UINT uPageCnt) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentPage(UINT * puPage) = 0;
};
#endif /* C++ */

#endif /* ITfCandidateListUIElement */

// WIDL headers omit ITfLangBarItemButton; MSVC defines it in msctf.h.
#if !defined(__ITfLangBarItemButton_INTERFACE_DEFINED__)
#define __ITfLangBarItemButton_INTERFACE_DEFINED__

#ifndef TF_LBI_STYLE_BTN_BUTTON
#define TF_LBI_STYLE_HIDDEN 0x00000001u
#define TF_LBI_STYLE_TEXTCOLOR 0x00000010u
#define TF_LBI_STYLE_BTN_BUTTON 0x00000100u
#define TF_LBI_STYLE_BTN_MENU 0x00000200u
#define TF_LBI_STYLE_BTN_TOGGLE 0x00000400u
#define TF_LBI_STYLE_SHOWNINTRAY 0x00000800u
#endif
#ifndef TF_LBI_STATUS_DISABLED
#define TF_LBI_STATUS_DISABLED 0x00000001u
#define TF_LBI_STATUS_HIDDEN 0x00000002u
#endif
#ifndef TF_LBI_ICON
#define TF_LBI_BMP 0x00000001u
#define TF_LBI_ICON 0x00000002u
#define TF_LBI_TEXT 0x00000004u
#define TF_LBI_TOOLTIP 0x00000008u
#define TF_LBI_STYLE 0x00000010u
#define TF_LBI_STATUS 0x00000020u
#endif
#ifndef TF_LBI_CLK_LEFT
#define TF_LBI_CLK_RIGHT 0
#define TF_LBI_CLK_LEFT 1
#endif

typedef int TfLBIClick;

struct ITfMenu;

EXTERN_C const IID IID_ITfLangBarItemButton;

#if defined(__cplusplus) && !defined(CINTERFACE)
MIDL_INTERFACE("6991dd3b-0e93-11d7-b1f0-00a024668130")
ITfLangBarItemButton : public ITfLangBarItem {
  public:
    virtual HRESULT STDMETHODCALLTYPE OnClick(TfLBIClick click, POINT pt,
                                              const RECT *prcArea) = 0;
    virtual HRESULT STDMETHODCALLTYPE InitMenu(ITfMenu * pMenu) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnMenuSelect(UINT wID) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetIcon(HICON * phIcon) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetText(BSTR * pbstrText) = 0;
    virtual HRESULT STDMETHODCALLTYPE AdviseSink(REFIID riid, IUnknown * punk,
                                                 DWORD * pdwCookie) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnadviseSink(DWORD dwCookie) = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(ITfLangBarItemButton, 0x6991dd3b, 0x0e93, 0x11d7, 0xb1, 0xf0,
                0x00, 0xa0, 0x24, 0x66, 0x81, 0x30)
#endif
#endif /* C++ */

#endif /* ITfLangBarItemButton */
