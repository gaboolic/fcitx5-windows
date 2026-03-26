#pragma once

#include "CandidateWindow.h"
#include "ImeEngine.h"

#include <msctf.h>
#include "MsctfMingwCompat.h"
#include <wrl/client.h>

#include <Windows.h>
#include <filesystem>
#include <sstream>
#include <memory>
#include <string>
#include <vector>

namespace fcitx {

inline std::filesystem::path tsfTraceLogPath() {
    wchar_t appData[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    std::filesystem::path dir;
    if (len > 0 && len < MAX_PATH) {
        dir = std::filesystem::path(appData) / "Fcitx5";
    } else {
        wchar_t tempPath[MAX_PATH] = {};
        len = GetTempPathW(MAX_PATH, tempPath);
        if (len > 0 && len < MAX_PATH) {
            dir = std::filesystem::path(tempPath);
        } else {
            dir = std::filesystem::temp_directory_path();
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / "tsf-trace.log";
}

inline void tsfTrace(const std::string &message) {
    const auto path = tsfTraceLogPath();
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    std::ostringstream ss;
    ss << "[pid=" << GetCurrentProcessId() << "] " << message << "\r\n";
    const std::string line = ss.str();
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written,
              nullptr);
    CloseHandle(file);
}

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;
class FcitxLangBarButton;
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

    void langBarScheduleToggleChinese();
    /// Tray / LangBar menu: switch to Chinese (true) or English pass-through (false).
    void langBarScheduleSetChineseMode(bool wantChinese);
    void langBarScheduleActivateInputMethod(const std::string &uniqueName);
    void langBarNotifyIconUpdate();
    bool langBarChineseMode() const { return chineseActive_; }
    bool sharedTrayChineseModeRequestPending() const;
    bool sharedTrayInputMethodRequestPending() const;
    bool scheduleSharedTrayChineseModeRequest(ITfContext *preferredContext = nullptr);
    bool scheduleSharedTrayInputMethodRequest(ITfContext *preferredContext = nullptr);
    HWND shellTrayHostHwnd() const { return shellTrayHostHwnd_; }

  private:
    friend class CandidateListUiElement;
    friend class FcitxLangBarButton;

    LONG refCount_ = 1;

    // ITfThreadMgrEventSink
    bool initLangBarTrayItem();
    void uninitLangBarTrayItem();
    void traySetChineseModeInEditSession(TfEditCookie ec, bool wantChinese);
    void trayToggleChineseInEditSession(TfEditCookie ec);
    void trayToggleChineseWithoutContext();
    bool initShellTrayIcon();
    void uninitShellTrayIcon();
    void updateShellTrayTooltip();
    void recreateShellTrayIcon();
    void scheduleShellTrayRetry(UINT delayMs = 1000);
    void cancelShellTrayRetry();
    void showShellTrayContextMenu();
    void showShellTrayContextMenuAt(POINT pt, HWND owner);
    void persistSharedTrayInputMethodRequest(const std::string &uniqueName) const;
    void clearSharedTrayInputMethodRequest() const;
    static LRESULT CALLBACK shellTrayHostWndProc(HWND hwnd, UINT msg, WPARAM wp,
                                                 LPARAM lp);

    bool initThreadMgrEventSink();
    void uninitThreadMgrEventSink();
    ComPtr<ITfThreadMgr> threadMgr_;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    DWORD threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;

    // ITfTextEditSink
    bool initTextEditSink(ITfDocumentMgr *documentMgr);
    DWORD textEditSinkCookie_ = TF_INVALID_COOKIE;
    ComPtr<ITfContext> textEditSinkContext_;
    /// Kept when TSF sink clears on tray focus loss; used to run tray toggle edit session.
    ComPtr<ITfContext> trayEditContextFallback_;

    // ITfKeyEventSink
    bool initKeyEventSink();
    void uninitKeyEventSink();
    BOOL processKey(WPARAM wParam, LPARAM lParam);
    BOOL processKeyUp(WPARAM wParam, LPARAM lParam);
    bool canProcessKeyDown(WPARAM wParam, LPARAM lParam);
    bool canProcessKeyUp(WPARAM wParam, LPARAM lParam) const;
    void trackShiftToggleKeyDown(WPARAM wParam, LPARAM lParam);
    void trackShiftToggleKeyUp(WPARAM wParam, LPARAM lParam);
    void resetShiftToggleGesture();

    // Composition + candidates: logic in ImeEngine (default Stub).
    CandidateWindow candidateWin_;
    // Start in Chinese mode: letters go to fcitx; Ctrl+Space toggles to pass-through English.
    bool chineseActive_ = true;
    FcitxLangBarButton *langBarItem_ = nullptr;
    HWND shellTrayHostHwnd_ = nullptr;
    bool shellTrayHostDllPinned_ = false;
    bool shellTrayHostClosing_ = false;
    HICON shellTrayIcon_ = nullptr;
    bool shellTrayIconOwned_ = false;
    bool shellTrayAdded_ = false;
    bool shellTrayUseGuidIdentity_ = true;
    bool shellTrayRetryPending_ = false;
    static UINT taskbarCreatedMessage_;
    bool pendingTrayToggleChinese_ = false;
    bool pendingTraySetChineseMode_ = false;
    bool pendingTraySetChineseModeValid_ = false;
    std::string pendingTrayInputMethod_;
    bool pendingTrayInputMethodFromSharedRequest_ = false;
    std::string deferredSharedTrayInputMethod_;
    std::unique_ptr<ImeEngine> engine_;
    ComPtr<ITfCandidateListUIElement> candidateListUi_;
    DWORD candidateUiElementId_ = TF_INVALID_UIELEMENTID;
    ComPtr<ITfComposition> composition_;
    ComPtr<ITfRange> compositionRange_;
    WPARAM pendingKeyWParam_ = 0;
    LPARAM pendingKeyLParam_ = 0;
    bool pendingKeyIsRelease_ = false;
    bool pendingKeyHandled_ = false;
    int pendingMousePick_ = -1;
    /// Plain Shift tap toggles Chinese / English (Shift+letter cancels).
    bool shiftTapTrack_ = false;
    bool shiftTapInvalidated_ = false;
    bool shiftTapTogglePending_ = false;
    WPARAM shiftTapTrackedWParam_ = 0;
    unsigned shiftTapTrackedScanCode_ = 0;

    bool keyWouldBeHandled(WPARAM wParam, LPARAM lParam);
    bool keyUpWouldBeHandled(WPARAM wParam, LPARAM lParam) const;
    HRESULT runKeyEditSession(TfEditCookie ec, WPARAM wp, LPARAM lp,
                              bool isRelease);
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
