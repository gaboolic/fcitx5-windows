#pragma once

#include "CandidateWindow.h"
#include "ImeEngine.h"

#include <msctf.h>
#include "MsctfMingwCompat.h"
#include <wrl/client.h>

#include <memory>
#include <string>
#include <vector>

namespace fcitx {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;
class Tsf : public ITfTextInputProcessorEx,
            public ITfThreadMgrEventSink,
            public ITfTextEditSink,
            public ITfKeyEventSink,
            public ITfCompositionSink,
            public ITfEditSession {
  public:
    /// When \p engine is null, uses `makeStubImeEngine()` (tests / DLL default).
    explicit Tsf(std::unique_ptr<ImeEngine> engine = nullptr);
    ~Tsf();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr *pThreadMgr,
                          TfClientId tfClientId) override;
    STDMETHODIMP Deactivate() override;

    // ITfTextInputProcessorEx
    STDMETHODIMP ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId,
                            DWORD dwFlags) override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr *pDocMgr) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr *pDocMgr) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr *pDocMgrFocus,
                            ITfDocumentMgr *pDocMgrPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext *pContext) override;
    STDMETHODIMP OnPopContext(ITfContext *pContext) override;

    // ITfTextEditSink
    STDMETHODIMP OnEndEdit(ITfContext *pic, TfEditCookie ecReadOnly,
                           ITfEditRecord *pEditRecord) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext *pContext, WPARAM wParam,
                               LPARAM lParam, BOOL *pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                           BOOL *pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                             BOOL *pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                         BOOL *pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext *pContext, REFGUID rguid,
                                BOOL *pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite,
                                       ITfComposition *pComposition) override;

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec) override;

  private:
    friend class CandidateListUiElement;

    LONG refCount_ = 1;

    // ITfThreadMgrEventSink
    bool initThreadMgrEventSink();
    void uninitThreadMgrEventSink();
    ComPtr<ITfThreadMgr> threadMgr_;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    DWORD threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;

    // ITfTextEditSink
    bool initTextEditSink(ITfDocumentMgr *documentMgr);
    DWORD textEditSinkCookie_ = TF_INVALID_COOKIE;
    ComPtr<ITfContext> textEditSinkContext_;

    // ITfKeyEventSink
    bool initKeyEventSink();
    void uninitKeyEventSink();
    BOOL processKey(WPARAM wParam, LPARAM lParam);
    BOOL keyDownHandled_ = false;

    // Composition + candidates: logic in ImeEngine (default Stub).
    CandidateWindow candidateWin_;
    // Start in Chinese mode: letters go to fcitx; Ctrl+Space toggles to pass-through English.
    bool chineseActive_ = true;
    std::unique_ptr<ImeEngine> engine_;
    ComPtr<ITfCandidateListUIElement> candidateListUi_;
    DWORD candidateUiElementId_ = TF_INVALID_UIELEMENTID;
    ComPtr<ITfComposition> composition_;
    ComPtr<ITfRange> compositionRange_;
    WPARAM pendingKeyWParam_ = 0;
    LPARAM pendingKeyLParam_ = 0;
    int pendingMousePick_ = -1;

    bool keyWouldBeHandled(WPARAM wParam, LPARAM lParam);
    HRESULT runKeyEditSession(TfEditCookie ec, WPARAM wp, LPARAM lp);
    void endCompositionCommit(TfEditCookie ec, const std::wstring &text);
    void endCompositionCancel(TfEditCookie ec);
    bool ensureCompositionStarted(TfEditCookie ec);
    void updatePreeditText(TfEditCookie ec);
    void syncCandidateWindow(TfEditCookie ec);
    void syncCandidateListUiElement();
    void endCandidateListUiElement();
    HRESULT queryCandidateListDocumentMgr(ITfDocumentMgr **ppDim);
    void drainCommitsAfterEngine(TfEditCookie ec);
    void afterFcitxEngineKey(TfEditCookie ec);
    void resetCompositionState();
    // TSF geometry: composition/insertion range → screen coords; false → use caret fallback.
    bool queryCandidateAnchor(TfEditCookie ec, POINT *screenPt);
};
} // namespace fcitx
