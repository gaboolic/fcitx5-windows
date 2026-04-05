#include "CandidateWindow.h"

#include "../dll/register.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <sstream>
#include <vector>
#include <windowsx.h>

#ifndef USER_DEFAULT_SCREEN_DPI
#define USER_DEFAULT_SCREEN_DPI 96
#endif

#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
#include <WebView2.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl.h>
#include <wrl/client.h>
#endif

namespace fcitx {

namespace {
constexpr wchar_t kClassName[] = L"Fcitx5CandidateWnd";

std::string wideToUtf8(const std::wstring &text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr,
                                         0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        utf8.empty() ? nullptr : &utf8[0], size, nullptr,
                        nullptr);
    return utf8;
}

std::wstring utf8ToWide(const std::string &text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        wide.empty() ? nullptr : &wide[0], size);
    return wide;
}

std::string jsonEscape(const std::string &text) {
    std::string escaped;
    escaped.reserve(text.size() + 16);
    for (unsigned char ch : text) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (ch < 0x20) {
                char buf[7] = {};
                std::snprintf(buf, sizeof(buf), "\\u%04x",
                              static_cast<unsigned>(ch));
                escaped += buf;
            } else {
                escaped.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return escaped;
}

std::string candidateLabelUtf8(size_t index) {
    if (index < 9) {
        return std::to_string(index + 1) + ".";
    }
    if (index == 9) {
        return "0.";
    }
    return std::to_string(index + 1) + ".";
}

std::string buildCandidatesJson(const std::vector<std::wstring> &candidates) {
    std::string json = "[";
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (i != 0) {
            json.push_back(',');
        }
        json += "{\"text\":\"";
        json += jsonEscape(wideToUtf8(candidates[i]));
        json += "\",\"label\":\"";
        json += jsonEscape(candidateLabelUtf8(i));
        json +=
            "\",\"comment\":\"\",\"actions\":[],\"spaceBetweenComment\":true}";
    }
    json += "]";
    return json;
}

std::wstring moduleDirectory() {
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(dllInstance, modulePath, MAX_PATH);
    if (len == 0) {
        return L".";
    }
    std::wstring path(modulePath, modulePath + len);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L".";
    }
    return path.substr(0, slash);
}

std::wstring appendPath(const std::wstring &base, const wchar_t *segment) {
    if (base.empty()) {
        return std::wstring(segment ? segment : L"");
    }
    if (base.back() == L'\\' || base.back() == L'/') {
        return base + std::wstring(segment ? segment : L"");
    }
    return base + L'\\' + std::wstring(segment ? segment : L"");
}

#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
std::wstring candidateHtmlUrl() {
    const std::wstring htmlPath = appendPath(
        appendPath(moduleDirectory(), L"fcitx5-webview"), L"index.html");
    DWORD urlLen = 0;
    UrlCreateFromPathW(htmlPath.c_str(), nullptr, &urlLen, 0);
    if (urlLen == 0) {
        return {};
    }
    std::wstring url(static_cast<size_t>(urlLen), L'\0');
    if (FAILED(UrlCreateFromPathW(htmlPath.c_str(), url.data(), &urlLen, 0))) {
        return {};
    }
    if (!url.empty() && url.back() == L'\0') {
        url.pop_back();
    }
    return url;
}

std::wstring candidateUserDataDirectory() {
    wchar_t base[MAX_PATH] = {};
    std::wstring dir;
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                                   SHGFP_TYPE_CURRENT, base))) {
        dir = base;
    } else {
        const DWORD len = GetTempPathW(MAX_PATH, base);
        dir.assign(base, base + len);
        while (!dir.empty() &&
               (dir.back() == L'\\' || dir.back() == L'/' || dir.back() == 0)) {
            dir.pop_back();
        }
    }
    dir = appendPath(dir, L"Fcitx5");
    dir = appendPath(dir, L"WebView2");
    dir = appendPath(dir, L"CandidateWindow");
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

// Wraps window.fcitx(...) so host messages reach WebView2 postMessage (string
// protocol).
constexpr wchar_t kHostBridgeScript[] =
    LR"((function(){var q=window.chrome&&window.chrome.webview;if(!q)return;var t=setInterval(function(){if(!window.fcitx||typeof window.fcitx.setCandidates!=='function')return;if(window.fcitx.__fcitxWinHost){clearInterval(t);return;}var inner=window.fcitx;var w=function(){var a=arguments;if(!a.length)return;var n=a[0];if(n==='onload')q.postMessage('onload');else if(n==='log'&&a.length>1)q.postMessage('log:'+String(a[1]));else if(n==='select'&&a.length>1)q.postMessage('select:'+a[1]);else if(n==='resize'&&a.length>1)q.postMessage('resize:'+Array.prototype.slice.call(a,1).join(','));};Object.setPrototypeOf(w,Object.getPrototypeOf(inner));for(var k in inner){try{if(Object.prototype.hasOwnProperty.call(inner,k))w[k]=inner[k];}catch(e){}}w.__fcitxWinHost=1;window.fcitx=w;clearInterval(t);},5);})();)";

using Microsoft::WRL::ComPtr;

using CreateCoreWebView2EnvironmentWithOptionsFn = HRESULT(STDAPICALLTYPE *)(
    PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);
#endif

} // namespace

#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
struct CandidateWindow::WebViewState {
    HMODULE loaderModule = nullptr;
    CreateCoreWebView2EnvironmentWithOptionsFn createEnvironment = nullptr;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    EventRegistrationToken messageToken = {};
    EventRegistrationToken navigationToken = {};
    bool initStarted = false;
    bool initFailed = false;
    bool pageReady = false;
    bool syncPending = false;
};

template <typename Interface> const IID &comInterfaceIid();

template <>
const IID &
comInterfaceIid<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>() {
    return IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler;
}

template <>
const IID &
comInterfaceIid<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>() {
    return IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler;
}

template <>
const IID &comInterfaceIid<ICoreWebView2WebMessageReceivedEventHandler>() {
    return IID_ICoreWebView2WebMessageReceivedEventHandler;
}

template <>
const IID &comInterfaceIid<ICoreWebView2NavigationCompletedEventHandler>() {
    return IID_ICoreWebView2NavigationCompletedEventHandler;
}

template <>
const IID &comInterfaceIid<ICoreWebView2ExecuteScriptCompletedHandler>() {
    return IID_ICoreWebView2ExecuteScriptCompletedHandler;
}

template <typename Interface> class ComCallbackBase : public Interface {
  public:
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        *ppvObject = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, comInterfaceIid<Interface>())) {
            *ppvObject = static_cast<Interface *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return ++refCount_; }

    STDMETHODIMP_(ULONG) Release() override {
        const ULONG ref = --refCount_;
        if (ref == 0) {
            delete this;
        }
        return ref;
    }

  protected:
    virtual ~ComCallbackBase() = default;

  private:
    std::atomic_ulong refCount_{1};
};

class EnvironmentCompletedHandler
    : public ComCallbackBase<
          ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> {
  public:
    explicit EnvironmentCompletedHandler(CandidateWindow *owner)
        : owner_(owner) {}

    STDMETHODIMP Invoke(HRESULT result,
                        ICoreWebView2Environment *env) override {
        return owner_ ? owner_->onWebViewEnvironmentCreated(result, env) : S_OK;
    }

  private:
    CandidateWindow *owner_;
};

class ControllerCompletedHandler
    : public ComCallbackBase<
          ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> {
  public:
    explicit ControllerCompletedHandler(CandidateWindow *owner)
        : owner_(owner) {}

    STDMETHODIMP Invoke(HRESULT result,
                        ICoreWebView2Controller *controller) override {
        return owner_ ? owner_->onWebViewControllerCreated(result, controller)
                      : S_OK;
    }

  private:
    CandidateWindow *owner_;
};

class MessageReceivedHandler
    : public ComCallbackBase<ICoreWebView2WebMessageReceivedEventHandler> {
  public:
    explicit MessageReceivedHandler(CandidateWindow *owner) : owner_(owner) {}

    STDMETHODIMP
    Invoke(ICoreWebView2 *,
           ICoreWebView2WebMessageReceivedEventArgs *args) override {
        return owner_ ? owner_->onWebViewMessage(args) : S_OK;
    }

  private:
    CandidateWindow *owner_;
};

class NavigationCompletedHandler
    : public ComCallbackBase<ICoreWebView2NavigationCompletedEventHandler> {
  public:
    explicit NavigationCompletedHandler(CandidateWindow *owner)
        : owner_(owner) {}

    STDMETHODIMP
    Invoke(ICoreWebView2 *,
           ICoreWebView2NavigationCompletedEventArgs *args) override {
        return owner_ ? owner_->onWebViewNavigationCompleted(args) : S_OK;
    }

  private:
    CandidateWindow *owner_;
};

class ExecuteScriptCompletedHandler
    : public ComCallbackBase<ICoreWebView2ExecuteScriptCompletedHandler> {
  public:
    ExecuteScriptCompletedHandler() = default;

    STDMETHODIMP Invoke(HRESULT errorCode,
                        LPCWSTR resultObjectAsJson) override {
        (void)resultObjectAsJson;
        if (FAILED(errorCode)) {
            RegisterTrace("CandidateWindow ExecuteScript failed");
        }
        return S_OK;
    }
};
#endif

CandidateWindow::CandidateWindow() = default;

CandidateWindow::~CandidateWindow() {
    destroyWebView();
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void CandidateWindow::ensureClass() {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = staticWndProc;
    wc.hInstance = dllInstance;
    wc.lpszClassName = kClassName;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.style = CS_SAVEBITS;
    RegisterClassExW(&wc);
    registered = true;
}

void CandidateWindow::ensureWindow() {
    ensureClass();
    if (hwnd_) {
        return;
    }
    hwnd_ =
        CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                        kClassName, L"", WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT,
                        100, 100, nullptr, nullptr, dllInstance, this);
}

LRESULT CALLBACK CandidateWindow::staticWndProc(HWND hwnd, UINT msg, WPARAM wp,
                                                LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
        auto *self = static_cast<CandidateWindow *>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    auto *self = reinterpret_cast<CandidateWindow *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        return self->handleMessage(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CandidateWindow::handleMessage(HWND hwnd, UINT msg, WPARAM wp,
                                       LPARAM lp) {
    switch (msg) {
    case WM_DESTROY:
        destroyWebView();
        hwnd_ = nullptr;
        return 0;
    case WM_ERASEBKGND:
#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
        if (webView_ && webView_->controller) {
            return 1;
        }
#endif
        break;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOVE:
    case WM_WINDOWPOSCHANGED:
#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
        if (webView_ && webView_->controller) {
            webView_->controller->NotifyParentWindowPositionChanged();
        }
#endif
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_SIZE:
        resizeWebView();
        return 0;
    case WM_DPICHANGED: {
        const RECT *suggested = reinterpret_cast<const RECT *>(lp);
        if (suggested) {
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
        }
        if (font_) {
            DeleteObject(font_);
            font_ = nullptr;
        }
        lastFontDpi_ = 0;
        layoutAndPaint();
        syncWebView();
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT cr;
        GetClientRect(hwnd, &cr);
#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
        if (!webView_ || !webView_->controller || !webView_->pageReady) {
            paintFallback(hdc, cr);
        }
#else
        paintFallback(hdc, cr);
#endif
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
        if (webView_ && webView_->controller) {
            return 0;
        }
#endif
        const int idx = hitTestLine(GET_Y_LPARAM(lp));
        if (idx >= 0 && idx < static_cast<int>(candidates_.size()) && onPick_) {
            onPick_(idx);
        }
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void CandidateWindow::clampToWorkArea(int *screenX, int *screenY) const {
    if (!screenX || !screenY || popupW_ <= 0 || popupH_ <= 0) {
        return;
    }
    POINT p = {*screenX, *screenY};
    HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) {
        return;
    }
    const RECT &wr = mi.rcWork;
    if (*screenX + popupW_ > wr.right) {
        *screenX = wr.right - popupW_;
    }
    if (*screenY + popupH_ > wr.bottom) {
        *screenY = wr.bottom - popupH_;
    }
    if (*screenX < wr.left) {
        *screenX = wr.left;
    }
    if (*screenY < wr.top) {
        *screenY = wr.top;
    }
}

void CandidateWindow::clampWindowToWorkArea() {
    if (!hwnd_) {
        return;
    }
    RECT wr = {};
    if (!GetWindowRect(hwnd_, &wr)) {
        return;
    }
    popupW_ = wr.right - wr.left;
    popupH_ = wr.bottom - wr.top;
    int x = wr.left;
    int y = wr.top;
    clampToWorkArea(&x, &y);
    if (x != wr.left || y != wr.top) {
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

int CandidateWindow::hitTestLine(int y) const {
    if (lineHeight_ <= 0) {
        return -1;
    }
    const int inner = y - padY_;
    if (inner < 0) {
        return -1;
    }
    return inner / lineHeight_;
}

std::wstring CandidateWindow::candidateLabel(size_t index) const {
    if (index < 9) {
        return std::to_wstring(index + 1) + L".";
    }
    if (index == 9) {
        return L"0.";
    }
    return std::to_wstring(index + 1) + L".";
}

std::wstring CandidateWindow::candidateDisplayText(size_t index) const {
    if (index >= candidates_.size()) {
        return {};
    }
    return candidateLabel(index) + L" " + candidates_[index];
}

void CandidateWindow::paintFallback(HDC hdc, const RECT &cr) {
    FillRect(hdc, &cr, (HBRUSH)(COLOR_WINDOW + 1));
    HFONT old = nullptr;
    if (font_) {
        old = (HFONT)SelectObject(hdc, font_);
    }
    SetBkMode(hdc, TRANSPARENT);
    for (size_t i = 0; i < candidates_.size(); ++i) {
        RECT line = cr;
        line.top = static_cast<LONG>(padY_ + static_cast<int>(i) * lineHeight_);
        line.bottom = line.top + lineHeight_;
        if (static_cast<int>(i) == highlight_) {
            HBRUSH hi = CreateSolidBrush(RGB(200, 220, 255));
            FillRect(hdc, &line, hi);
            DeleteObject(hi);
        }
        RECT textRc = line;
        textRc.left += padX_;
        const std::wstring text = candidateDisplayText(i);
        DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &textRc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    if (old) {
        SelectObject(hdc, old);
    }
}

void CandidateWindow::layoutAndPaint() {
    if (!hwnd_) {
        return;
    }
    const UINT dpi = GetDpiForWindow(hwnd_);
    padX_ = MulDiv(8, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    padY_ = MulDiv(6, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    HDC hdc = GetDC(hwnd_);
    HFONT old = nullptr;
    if (font_) {
        old = (HFONT)SelectObject(hdc, font_);
    }
    int maxW = MulDiv(80, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    for (size_t i = 0; i < candidates_.size(); ++i) {
        const std::wstring s = candidateDisplayText(i);
        SIZE sz = {};
        GetTextExtentPoint32W(hdc, s.c_str(), static_cast<int>(s.size()), &sz);
        maxW = (std::max)(maxW, static_cast<int>(sz.cx));
    }
    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);
    lineHeight_ = tm.tmHeight + tm.tmExternalLeading + 2;
    if (old) {
        SelectObject(hdc, old);
    }
    ReleaseDC(hwnd_, hdc);

    popupH_ = padY_ * 2 + lineHeight_ * static_cast<int>(candidates_.size());
    popupW_ = padX_ * 2 + maxW;
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, popupW_, popupH_,
                 SWP_NOMOVE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    resizeWebView();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void CandidateWindow::resizeWebView() {
#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
    if (!hwnd_ || !webView_ || !webView_->controller) {
        return;
    }
    RECT bounds = {};
    GetClientRect(hwnd_, &bounds);
    webView_->controller->put_Bounds(bounds);
#endif
}

void CandidateWindow::destroyWebView() {
#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
    if (!webView_) {
        return;
    }
    if (webView_->webview) {
        webView_->webview->remove_WebMessageReceived(webView_->messageToken);
        webView_->webview->remove_NavigationCompleted(
            webView_->navigationToken);
    }
    webView_->webview.Reset();
    webView_->controller.Reset();
    webView_->environment.Reset();
    if (webView_->loaderModule) {
        FreeLibrary(webView_->loaderModule);
        webView_->loaderModule = nullptr;
    }
    webView_.reset();
#endif
}

void CandidateWindow::ensureWebView() {
#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
    if (!hwnd_) {
        return;
    }
    if (!webView_) {
        webView_ = std::make_unique<WebViewState>();
    }
    if (webView_->initStarted || webView_->initFailed || webView_->controller) {
        return;
    }
    std::wstring loaderPath =
        appendPath(moduleDirectory(), L"WebView2Loader.dll");
    webView_->loaderModule = LoadLibraryW(loaderPath.c_str());
    if (!webView_->loaderModule) {
        webView_->loaderModule = LoadLibraryW(L"WebView2Loader.dll");
    }
    if (!webView_->loaderModule) {
        webView_->initFailed = true;
        return;
    }
    webView_->createEnvironment =
        reinterpret_cast<CreateCoreWebView2EnvironmentWithOptionsFn>(
            GetProcAddress(webView_->loaderModule,
                           "CreateCoreWebView2EnvironmentWithOptions"));
    if (!webView_->createEnvironment) {
        webView_->initFailed = true;
        return;
    }
    const std::wstring userDataDir = candidateUserDataDirectory();
    webView_->initStarted = true;
    auto *envHandler = new EnvironmentCompletedHandler(this);
    const HRESULT hr = webView_->createEnvironment(nullptr, userDataDir.c_str(),
                                                   nullptr, envHandler);
    envHandler->Release();
    if (FAILED(hr)) {
        webView_->initFailed = true;
    }
#endif
}

void CandidateWindow::syncWebView() {
#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
    if (!webView_ || !webView_->controller || !webView_->webview) {
        return;
    }
    webView_->controller->put_IsVisible(isVisible() ? TRUE : FALSE);
    if (!webView_->pageReady) {
        webView_->syncPending = true;
        return;
    }
    webView_->syncPending = false;
    pushWebViewState();
#endif
}

#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
void CandidateWindow::pushWebViewState() {
    if (!webView_ || !webView_->webview || candidates_.empty()) {
        return;
    }
    ++webViewEpoch_;
    std::ostringstream script;
    script << "(function(){try{if(!window.fcitx)return;";
    script << "window.fcitx.setHost('Windows',11);";
    script << "window.fcitx.setTheme(0);";
    script << "window.fcitx.setLayout(0);";
    script << "window.fcitx.setWritingMode(0);";
    script << "window.fcitx.updateInputPanel([],false,[],[],[]);";
    script << "window.fcitx.setCandidates(" << buildCandidatesJson(candidates_)
           << "," << highlight_ << ",false,false,false,0,false,false);";
    script << "window.fcitx.resize(" << webViewEpoch_
           << ",0,0,false,false);}catch(e){}})();";
    const std::wstring w = utf8ToWide(script.str());
    auto *done = new ExecuteScriptCompletedHandler();
    webView_->webview->ExecuteScript(w.c_str(), done);
    done->Release();
}

void CandidateWindow::applyWebViewLayoutRect(int contentRight,
                                             int contentBottom, double dx,
                                             double dy, bool dragging) {
    if (!hwnd_) {
        return;
    }
    int cw = contentRight;
    int ch = contentBottom;
    if (cw < 1) {
        cw = 1;
    }
    if (ch < 1) {
        ch = 1;
    }
    RECT clientRc = {0, 0, cw, ch};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
    const DWORD exStyle =
        static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
    AdjustWindowRectEx(&clientRc, style, FALSE, exStyle);
    const int outerW = clientRc.right - clientRc.left;
    const int outerH = clientRc.bottom - clientRc.top;
    RECT winRc = {};
    GetWindowRect(hwnd_, &winRc);
    int x = winRc.left;
    int y = winRc.top;
    if (dragging) {
        x += static_cast<int>(std::lround(dx));
        y += static_cast<int>(std::lround(dy));
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, outerW, outerH, SWP_NOACTIVATE);
    popupW_ = outerW;
    popupH_ = outerH;
    resizeWebView();
    if (webView_ && webView_->controller) {
        webView_->controller->NotifyParentWindowPositionChanged();
    }
    clampWindowToWorkArea();
}

void CandidateWindow::handleWebViewHostMessage(const std::wstring &msg) {
    if (msg == L"onload" || msg == L"ready") {
        RegisterTrace("CandidateWindow webview onload");
        webView_->pageReady = true;
        syncWebView();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return;
    }
    if (msg.rfind(L"log:", 0) == 0) {
        RegisterTrace("CandidateWindow webview: " + wideToUtf8(msg.substr(4)));
        return;
    }
    if (msg.rfind(L"select:", 0) == 0 && onPick_) {
        const int idx = _wtoi(msg.c_str() + 7);
        if (idx >= 0 && idx < static_cast<int>(candidates_.size())) {
            onPick_(idx);
        }
        return;
    }
    if (msg.rfind(L"pick:", 0) == 0 && onPick_) {
        const int idx = _wtoi(msg.c_str() + 5);
        if (idx >= 0 && idx < static_cast<int>(candidates_.size())) {
            onPick_(idx);
        }
        return;
    }
    if (msg.rfind(L"resize:", 0) == 0) {
        const std::wstring csv = msg.substr(7);
        std::vector<std::wstring> parts;
        size_t start = 0;
        for (size_t i = 0; i <= csv.size(); ++i) {
            if (i == csv.size() || csv[i] == L',') {
                parts.emplace_back(csv.substr(start, i - start));
                start = i + 1;
            }
        }
        if (parts.size() < 19) {
            return;
        }
        double v[18];
        for (int i = 0; i < 18; ++i) {
            v[i] = std::wcstod(parts[static_cast<size_t>(i)].c_str(), nullptr);
        }
        const std::wstring &dragTok = parts[18];
        const bool dragging =
            dragTok == L"true" || dragTok == L"1" || dragTok == L"True";
        const int cr = static_cast<int>(std::ceil(v[16]));
        const int cb = static_cast<int>(std::ceil(v[17]));
        applyWebViewLayoutRect(cr, cb, v[1], v[2], dragging);
        return;
    }
}

HRESULT CandidateWindow::onWebViewEnvironmentCreated(HRESULT result,
                                                     void *envRaw) {
    auto *env = static_cast<ICoreWebView2Environment *>(envRaw);
    if (!webView_) {
        return S_OK;
    }
    if (FAILED(result) || !env || !hwnd_) {
        webView_->initFailed = true;
        return S_OK;
    }
    webView_->environment = env;
    auto *controllerHandler = new ControllerCompletedHandler(this);
    const HRESULT hr =
        env->CreateCoreWebView2Controller(hwnd_, controllerHandler);
    controllerHandler->Release();
    return hr;
}

HRESULT CandidateWindow::onWebViewControllerCreated(HRESULT result,
                                                    void *controllerRaw) {
    auto *controller = static_cast<ICoreWebView2Controller *>(controllerRaw);
    if (!webView_) {
        return S_OK;
    }
    if (FAILED(result) || !controller) {
        webView_->initFailed = true;
        return S_OK;
    }
    webView_->controller = controller;
    if (FAILED(controller->get_CoreWebView2(
            webView_->webview.ReleaseAndGetAddressOf())) ||
        !webView_->webview) {
        webView_->initFailed = true;
        return S_OK;
    }
    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(webView_->webview->get_Settings(
            settings.ReleaseAndGetAddressOf())) &&
        settings) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
    }
    ComPtr<ICoreWebView2Controller2> controller2;
    if (SUCCEEDED(controller->QueryInterface(
            IID_ICoreWebView2Controller2,
            reinterpret_cast<void **>(controller2.ReleaseAndGetAddressOf()))) &&
        controller2) {
        COREWEBVIEW2_COLOR color = {0, 0, 0, 0};
        controller2->put_DefaultBackgroundColor(color);
    }
    webView_->webview->AddScriptToExecuteOnDocumentCreated(kHostBridgeScript,
                                                           nullptr);
    auto *messageHandler = new MessageReceivedHandler(this);
    webView_->webview->add_WebMessageReceived(messageHandler,
                                              &webView_->messageToken);
    messageHandler->Release();
    auto *navigationHandler = new NavigationCompletedHandler(this);
    webView_->webview->add_NavigationCompleted(navigationHandler,
                                               &webView_->navigationToken);
    navigationHandler->Release();
    resizeWebView();
    webView_->controller->put_IsVisible(isVisible() ? TRUE : FALSE);
    const std::wstring url = candidateHtmlUrl();
    if (url.empty() || FAILED(webView_->webview->Navigate(url.c_str()))) {
        webView_->initFailed = true;
    }
    return S_OK;
}

HRESULT CandidateWindow::onWebViewMessage(void *argsRaw) {
    auto *args =
        static_cast<ICoreWebView2WebMessageReceivedEventArgs *>(argsRaw);
    if (!webView_ || !args) {
        return S_OK;
    }
    LPWSTR raw = nullptr;
    if (FAILED(args->TryGetWebMessageAsString(&raw)) || !raw) {
        return S_OK;
    }
    const std::wstring msg(raw);
    CoTaskMemFree(raw);
    constexpr wchar_t errorPrefix[] = L"error:";
    constexpr size_t errorPrefixLen = 6;
    if (msg.rfind(errorPrefix, 0) == 0) {
        RegisterTrace("CandidateWindow webview error: " +
                      wideToUtf8(msg.substr(errorPrefixLen)));
        return S_OK;
    }
    handleWebViewHostMessage(msg);
    return S_OK;
}

HRESULT CandidateWindow::onWebViewNavigationCompleted(void *argsRaw) {
    auto *args =
        static_cast<ICoreWebView2NavigationCompletedEventArgs *>(argsRaw);
    BOOL ok = TRUE;
    if (args) {
        args->get_IsSuccess(&ok);
    }
    if (!ok || !webView_) {
        return S_OK;
    }
    webView_->pageReady = true;
    syncWebView();
    InvalidateRect(hwnd_, nullptr, TRUE);
    return S_OK;
}
#endif

void CandidateWindow::show(int screenX, int screenY,
                           const std::vector<std::wstring> &candidates,
                           int highlightIndex) {
    candidates_ = candidates;
    highlight_ = highlightIndex;
    anchorX_ = screenX;
    anchorY_ = screenY;
    if (!candidates_.empty()) {
        if (highlight_ < 0) {
            highlight_ = 0;
        }
        const int n = static_cast<int>(candidates_.size());
        if (highlight_ >= n) {
            highlight_ = n - 1;
        }
    }
    if (candidates_.empty()) {
        hide();
        return;
    }
    ensureWindow();
    if (!hwnd_) {
        return;
    }
    const UINT dpi = GetDpiForWindow(hwnd_);
    if (!font_ || lastFontDpi_ != dpi) {
        if (font_) {
            DeleteObject(font_);
            font_ = nullptr;
        }
        lastFontDpi_ = dpi;
        const int fontPx =
            MulDiv(14, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        font_ = CreateFontW(-fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    layoutAndPaint();
    clampToWorkArea(&screenX, &screenY);
    SetWindowPos(hwnd_, HWND_TOPMOST, screenX, screenY, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(hwnd_, SW_SHOWNA);
    ensureWebView();
    syncWebView();
    UpdateWindow(hwnd_);
}

void CandidateWindow::hide() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);
    }
#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
    if (webView_ && webView_->controller) {
        webView_->controller->put_IsVisible(FALSE);
    }
#endif
    candidates_.clear();
}

} // namespace fcitx
