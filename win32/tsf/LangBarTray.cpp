#include "tsf.h"
#include "LangBarTray.h"
#include "../dll/util.h"

#include <filesystem>
#include <oleauto.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>

#ifndef CONNECT_E_NOCONNECTION
#define CONNECT_E_NOCONNECTION static_cast<HRESULT>(0x80040200L)
#endif
#ifndef CONNECT_E_ADVISELIMIT
#define CONNECT_E_ADVISELIMIT static_cast<HRESULT>(0x80040203L)
#endif
#ifndef NIF_GUID
#define NIF_GUID 0x00000020
#endif
#ifndef NOTIFYICON_VERSION_4
#define NOTIFYICON_VERSION_4 4U
#endif

extern void DllAddRef();
extern void DllRelease();

namespace fcitx {
extern HINSTANCE dllInstance;

namespace {

const GUID kFcitxTrayLangBarItemId = {
    0xf7e8d9c0, 0xb1a2, 0x4e3f, {0x9d, 0x8c, 0x7e, 0x6f, 0x5a, 0x4b, 0x3c, 0x2d}};

/** Stable id for Shell_NotifyIcon so Windows can persist tray visibility per user. */
const GUID kFcitxShellTrayNotifyGuid = {
    0x8b4d3a2f, 0x1e0c, 0x4a5b, {0x9c, 0x8d, 0x7e, 0x6f, 0x5a, 0x4b, 0x3c, 0x2e}};

enum TrayMenuId : UINT {
    IDM_CHINESE = 0x4100,
    IDM_ENGLISH,
    IDM_SETTINGS_GUI,
    IDM_OPEN_CONFIG_DIR,
};

constexpr UINT kShellTrayCallback = WM_APP + 88;
constexpr UINT kShellTrayUid = 1;
/** Win32 resource id in fcitx5-ime.rc (fcitx5-x86_64.dll); keep in sync with .rc.in */
constexpr WORD kFcitxPenguinIconResId = 100;
const wchar_t kShellTrayHostClass[] = L"Fcitx5ShellTrayHost";
bool gShellTrayClassRegistered = false;

void fillShellTrayNidIdentity(NOTIFYICONDATAW *nid, HWND hostHwnd) {
    ZeroMemory(nid, sizeof(*nid));
    nid->cbSize = sizeof(*nid);
    nid->hWnd = hostHwnd;
    nid->uID = kShellTrayUid;
    nid->uFlags = NIF_GUID;
    nid->guidItem = kFcitxShellTrayNotifyGuid;
}

bool shellTrayNotifyAdd(HWND hostHwnd, HICON icon, bool chineseMode) {
    NOTIFYICONDATAW nid = {};
    fillShellTrayNidIdentity(&nid, hostHwnd);
    nid.uFlags |= NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kShellTrayCallback;
    nid.hIcon = icon;
    if (chineseMode) {
        wcsncpy_s(nid.szTip,
                  L"Fcitx5 \x2014 \x4e2d\x6587\nShift / Ctrl+Space \x5207\x6362\x4e2d/\x82f1",
                  _TRUNCATE);
    } else {
        wcsncpy_s(nid.szTip,
                  L"Fcitx5 \x2014 English\nShift / Ctrl+Space \x5207\x6362\x4e2d/\x82f1",
                  _TRUNCATE);
    }
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        return false;
    }
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    return true;
}

HICON loadPenguinIconNearDll(unsigned cx, unsigned cy) {
    HICON embedded = reinterpret_cast<HICON>(LoadImageW(
        dllInstance, MAKEINTRESOURCEW(kFcitxPenguinIconResId), IMAGE_ICON,
        static_cast<int>(cx), static_cast<int>(cy), LR_DEFAULTCOLOR));
    if (!embedded) {
        embedded = reinterpret_cast<HICON>(LoadImageW(
            dllInstance, MAKEINTRESOURCEW(kFcitxPenguinIconResId), IMAGE_ICON,
            0, 0, LR_DEFAULTCOLOR));
    }
    if (embedded) {
        return embedded;
    }
    WCHAR dllPath[MAX_PATH];
    if (!GetModuleFileNameW(dllInstance, dllPath, MAX_PATH)) {
        return nullptr;
    }
    std::filesystem::path p(dllPath);
    p = p.parent_path() / L"penguin.ico";
    return reinterpret_cast<HICON>(LoadImageW(
        nullptr, p.c_str(), IMAGE_ICON, static_cast<int>(cx), static_cast<int>(cy),
        LR_LOADFROMFILE));
}

void launchSettingsGui() {
    WCHAR dllPath[MAX_PATH];
    if (!GetModuleFileNameW(dllInstance, dllPath, MAX_PATH)) {
        return;
    }
    std::filesystem::path exe(dllPath);
    exe = exe.parent_path() / L"fcitx5-config-win32.exe";
    ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr,
                  exe.parent_path().c_str(), SW_SHOWNORMAL);
}

void exploreUserFcitxConfig() {
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return;
    }
    std::wstring dir = appData;
    dir += L"\\Fcitx5";
    ShellExecuteW(nullptr, L"explore", dir.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
}

} // namespace

FcitxLangBarButton::FcitxLangBarButton(Tsf *tsf)
    : tsf_(tsf), ref_(1), status_(0), sink_(nullptr) {
    DllAddRef();
}

FcitxLangBarButton::~FcitxLangBarButton() {
    if (sink_) {
        sink_->Release();
        sink_ = nullptr;
    }
    DllRelease();
}

void FcitxLangBarButton::notifyModeChanged() {
    if (sink_) {
        sink_->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON | TF_LBI_TOOLTIP);
    }
}

STDMETHODIMP FcitxLangBarButton::QueryInterface(REFIID riid, void **ppvObject) {
    if (!ppvObject) {
        return E_INVALIDARG;
    }
    *ppvObject = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton)) {
        *ppvObject = static_cast<ITfLangBarItemButton *>(this);
    }
    if (*ppvObject) {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) FcitxLangBarButton::AddRef() { return ++ref_; }

STDMETHODIMP_(ULONG) FcitxLangBarButton::Release() {
    const ULONG n = --ref_;
    if (n == 0) {
        delete this;
    }
    return n;
}

STDMETHODIMP FcitxLangBarButton::GetInfo(TF_LANGBARITEMINFO *pInfo) {
    if (!pInfo) {
        return E_INVALIDARG;
    }
    pInfo->clsidService = FCITX_CLSID;
    pInfo->guidItem = kFcitxTrayLangBarItemId;
    // TF_LBI_STYLE_SHOWNINTRAY is unreliable on Win10/11; shell notification
    // icon (initShellTrayIcon) provides the taskbar entry.
    pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_BTN_MENU;
    pInfo->ulSort = 0;
    wcsncpy_s(pInfo->szDescription, TF_LBI_DESC_MAXLEN, L"Fcitx5",
              _TRUNCATE);
    return S_OK;
}

STDMETHODIMP FcitxLangBarButton::GetStatus(DWORD *pdwStatus) {
    if (!pdwStatus) {
        return E_INVALIDARG;
    }
    *pdwStatus = status_;
    return S_OK;
}

STDMETHODIMP FcitxLangBarButton::Show(BOOL fShow) {
    const BOOL hide = fShow ? FALSE : TRUE;
    if (hide) {
        status_ |= TF_LBI_STATUS_HIDDEN;
    } else {
        status_ &= ~TF_LBI_STATUS_HIDDEN;
    }
    notifyModeChanged();
    return S_OK;
}

STDMETHODIMP FcitxLangBarButton::GetTooltipString(BSTR *pbstrToolTip) {
    if (!pbstrToolTip) {
        return E_INVALIDARG;
    }
    const wchar_t *s =
        tsf_ && tsf_->langBarChineseMode()
            ? L"Fcitx5 — 中文输入\nShift：中/英  Ctrl+Space：中/英\n右键：菜单"
            : L"Fcitx5 — English (pass-through)\nShift：中/英  Ctrl+Space：中/英\n右键：菜单";
    *pbstrToolTip = SysAllocString(s);
    return *pbstrToolTip ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP FcitxLangBarButton::OnClick(TfLBIClick click, POINT pt,
                                         const RECT *prcArea) {
    (void)prcArea;
    if (!tsf_) {
        return S_OK;
    }
    if (click == TF_LBI_CLK_LEFT) {
        tsf_->langBarScheduleToggleChinese();
        return S_OK;
    }
    if (click == TF_LBI_CLK_RIGHT) {
        HWND owner = GetForegroundWindow();
        if (!owner) {
            owner = GetDesktopWindow();
        }
        HMENU menu = CreatePopupMenu();
        if (!menu) {
            return S_OK;
        }
        AppendMenuW(menu, MF_STRING | (tsf_->langBarChineseMode() ? MF_CHECKED : 0),
                    IDM_CHINESE, L"中文输入");
        AppendMenuW(menu, MF_STRING | (!tsf_->langBarChineseMode() ? MF_CHECKED : 0),
                    IDM_ENGLISH, L"英文输入（直接键入）");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_SETTINGS_GUI, L"Fcitx5 设置…");
        AppendMenuW(menu, MF_STRING, IDM_OPEN_CONFIG_DIR, L"打开用户配置文件夹");
        const UINT cmd = TrackPopupMenu(menu,
                                        TPM_LEFTALIGN | TPM_TOPALIGN |
                                            TPM_RETURNCMD | TPM_NONOTIFY,
                                        pt.x, pt.y, 0, owner, nullptr);
        DestroyMenu(menu);
        switch (cmd) {
        case IDM_CHINESE:
            tsf_->langBarScheduleSetChineseMode(true);
            break;
        case IDM_ENGLISH:
            tsf_->langBarScheduleSetChineseMode(false);
            break;
        case IDM_SETTINGS_GUI:
            launchSettingsGui();
            break;
        case IDM_OPEN_CONFIG_DIR:
            exploreUserFcitxConfig();
            break;
        default:
            break;
        }
    }
    return S_OK;
}

STDMETHODIMP FcitxLangBarButton::InitMenu(ITfMenu *pMenu) {
    (void)pMenu;
    return S_OK;
}

STDMETHODIMP FcitxLangBarButton::OnMenuSelect(UINT wID) {
    (void)wID;
    return S_OK;
}

STDMETHODIMP FcitxLangBarButton::GetIcon(HICON *phIcon) {
    if (!phIcon) {
        return E_INVALIDARG;
    }
    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);
    *phIcon = loadPenguinIconNearDll(static_cast<unsigned>(cx),
                                     static_cast<unsigned>(cy));
    if (!*phIcon) {
        *phIcon = reinterpret_cast<HICON>(LoadImageW(
            nullptr, MAKEINTRESOURCEW(32512), IMAGE_ICON, static_cast<int>(cx),
            static_cast<int>(cy), LR_SHARED));
    }
    return *phIcon ? S_OK : E_FAIL;
}

STDMETHODIMP FcitxLangBarButton::GetText(BSTR *pbstrText) {
    if (!pbstrText) {
        return E_INVALIDARG;
    }
    *pbstrText = SysAllocString(L"Fcitx5");
    return *pbstrText ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP FcitxLangBarButton::AdviseSink(REFIID riid, IUnknown *punk,
                                            DWORD *pdwCookie) {
    if (!pdwCookie || !IsEqualIID(riid, IID_ITfLangBarItemSink)) {
        return E_INVALIDARG;
    }
    if (sink_ || !punk) {
        return CONNECT_E_ADVISELIMIT;
    }
    if (FAILED(punk->QueryInterface(IID_ITfLangBarItemSink,
                                     reinterpret_cast<void **>(&sink_)))) {
        sink_ = nullptr;
        return E_NOINTERFACE;
    }
    *pdwCookie = kSinkCookie;
    return S_OK;
}

STDMETHODIMP FcitxLangBarButton::UnadviseSink(DWORD dwCookie) {
    if (dwCookie != kSinkCookie || !sink_) {
        return CONNECT_E_NOCONNECTION;
    }
    sink_->Release();
    sink_ = nullptr;
    return S_OK;
}

bool Tsf::initLangBarTrayItem() {
    if (langBarItem_ || !threadMgr_) {
        initShellTrayIcon();
        return langBarItem_ != nullptr;
    }
    ComPtr<ITfLangBarItemMgr> mgr;
    if (FAILED(threadMgr_->QueryInterface(
            IID_ITfLangBarItemMgr,
            reinterpret_cast<void **>(mgr.ReleaseAndGetAddressOf())))) {
        initShellTrayIcon();
        return false;
    }
    auto *btn = new FcitxLangBarButton(this);
    if (FAILED(mgr->AddItem(btn))) {
        btn->Release();
        initShellTrayIcon();
        return false;
    }
    langBarItem_ = btn;
    initShellTrayIcon();
    return true;
}

void Tsf::uninitLangBarTrayItem() {
    uninitShellTrayIcon();
    if (!langBarItem_) {
        return;
    }
    if (threadMgr_) {
        ComPtr<ITfLangBarItemMgr> mgr;
        if (SUCCEEDED(threadMgr_->QueryInterface(
                IID_ITfLangBarItemMgr,
                reinterpret_cast<void **>(mgr.ReleaseAndGetAddressOf())))) {
            mgr->RemoveItem(langBarItem_);
        }
    }
    langBarItem_->Release();
    langBarItem_ = nullptr;
}

void Tsf::langBarScheduleToggleChinese() {
    pendingTrayToggleChinese_ = true;
    HRESULT hr = E_FAIL;
    ComPtr<ITfContext> ctx = textEditSinkContext_;
    if (!ctx && threadMgr_) {
        ComPtr<ITfDocumentMgr> dm;
        if (SUCCEEDED(threadMgr_->GetFocus(dm.ReleaseAndGetAddressOf())) && dm) {
            dm->GetTop(ctx.ReleaseAndGetAddressOf());
        }
    }
    if (!ctx) {
        ctx = trayEditContextFallback_;
    }
    if (ctx) {
        ctx->RequestEditSession(clientId_, this, TF_ES_SYNC | TF_ES_READWRITE,
                                &hr);
    }
    if (FAILED(hr)) {
        pendingTrayToggleChinese_ = false;
        trayToggleChineseWithoutContext();
    }
}

void Tsf::langBarScheduleSetChineseMode(bool wantChinese) {
    if (chineseActive_ == wantChinese) {
        langBarNotifyIconUpdate();
        return;
    }
    langBarScheduleToggleChinese();
}

void Tsf::langBarNotifyIconUpdate() {
    updateShellTrayTooltip();
    if (langBarItem_) {
        langBarItem_->notifyModeChanged();
    }
}

void Tsf::trayToggleChineseInEditSession(TfEditCookie ec) {
    chineseActive_ = !chineseActive_;
    if (!chineseActive_) {
        endCompositionCancel(ec);
    }
    syncCandidateWindow(ec);
    drainCommitsAfterEngine(ec);
    langBarNotifyIconUpdate();
}

void Tsf::trayToggleChineseWithoutContext() {
    chineseActive_ = !chineseActive_;
    candidateWin_.hide();
    endCandidateListUiElement();
    if (engine_) {
        engine_->clear();
    }
    langBarNotifyIconUpdate();
}

UINT Tsf::taskbarCreatedMessage_ = 0;

LRESULT CALLBACK Tsf::shellTrayHostWndProc(HWND hwnd, UINT msg, WPARAM wp,
                                           LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    Tsf *self = reinterpret_cast<Tsf *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (msg == kShellTrayCallback) {
        if (lp == WM_LBUTTONUP) {
            self->langBarScheduleToggleChinese();
        } else if (lp == WM_RBUTTONUP) {
            self->showShellTrayContextMenu();
        }
        return 0;
    }
    if (Tsf::taskbarCreatedMessage_ != 0 &&
        msg == Tsf::taskbarCreatedMessage_) {
        self->recreateShellTrayIcon();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool Tsf::initShellTrayIcon() {
    if (taskbarCreatedMessage_ == 0) {
        taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    }
    if (!shellTrayHostHwnd_) {
        if (!gShellTrayClassRegistered) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = shellTrayHostWndProc;
            wc.hInstance = dllInstance;
            wc.lpszClassName = kShellTrayHostClass;
            const ATOM a = RegisterClassExW(&wc);
            if (!a && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                return false;
            }
            gShellTrayClassRegistered = true;
        }
        shellTrayHostHwnd_ = CreateWindowExW(
            0, kShellTrayHostClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
            dllInstance, this);
        if (!shellTrayHostHwnd_) {
            return false;
        }
        const int cx = GetSystemMetrics(SM_CXSMICON);
        const int cy = GetSystemMetrics(SM_CYSMICON);
        shellTrayIcon_ = loadPenguinIconNearDll(static_cast<unsigned>(cx),
                                                static_cast<unsigned>(cy));
        if (shellTrayIcon_) {
            shellTrayIconOwned_ = true;
        } else {
            shellTrayIcon_ = reinterpret_cast<HICON>(LoadImageW(
                nullptr, MAKEINTRESOURCEW(32512), IMAGE_ICON, cx, cy,
                LR_SHARED));
            shellTrayIconOwned_ = false;
        }
    }
    if (!shellTrayIcon_) {
        return false;
    }
    if (shellTrayAdded_) {
        return true;
    }
    shellTrayAdded_ = shellTrayNotifyAdd(shellTrayHostHwnd_, shellTrayIcon_,
                                        langBarChineseMode());
    return shellTrayAdded_;
}

void Tsf::uninitShellTrayIcon() {
    if (shellTrayAdded_ && shellTrayHostHwnd_) {
        NOTIFYICONDATAW nid = {};
        fillShellTrayNidIdentity(&nid, shellTrayHostHwnd_);
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }
    shellTrayAdded_ = false;
    if (shellTrayHostHwnd_) {
        DestroyWindow(shellTrayHostHwnd_);
        shellTrayHostHwnd_ = nullptr;
    }
    if (shellTrayIconOwned_ && shellTrayIcon_) {
        DestroyIcon(shellTrayIcon_);
    }
    shellTrayIcon_ = nullptr;
    shellTrayIconOwned_ = false;
}

void Tsf::updateShellTrayTooltip() {
    if (!shellTrayAdded_ || !shellTrayHostHwnd_) {
        return;
    }
    NOTIFYICONDATAW nid = {};
    fillShellTrayNidIdentity(&nid, shellTrayHostHwnd_);
    nid.uFlags |= NIF_TIP;
    if (langBarChineseMode()) {
        wcsncpy_s(nid.szTip,
                  L"Fcitx5 \x2014 \x4e2d\x6587\nShift / Ctrl+Space \x5207\x6362\x4e2d/\x82f1",
                  _TRUNCATE);
    } else {
        wcsncpy_s(nid.szTip,
                  L"Fcitx5 \x2014 English\nShift / Ctrl+Space \x5207\x6362\x4e2d/\x82f1",
                  _TRUNCATE);
    }
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void Tsf::recreateShellTrayIcon() {
    if (!shellTrayHostHwnd_ || !shellTrayIcon_) {
        return;
    }
    if (shellTrayAdded_) {
        NOTIFYICONDATAW del = {};
        fillShellTrayNidIdentity(&del, shellTrayHostHwnd_);
        Shell_NotifyIconW(NIM_DELETE, &del);
        shellTrayAdded_ = false;
    }
    shellTrayAdded_ = shellTrayNotifyAdd(shellTrayHostHwnd_, shellTrayIcon_,
                                        langBarChineseMode());
}

void Tsf::showShellTrayContextMenu() {
    HWND owner = shellTrayHostHwnd_ ? shellTrayHostHwnd_ : GetForegroundWindow();
    if (!owner) {
        owner = GetDesktopWindow();
    }
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    AppendMenuW(
        menu, MF_STRING | (langBarChineseMode() ? MF_CHECKED : 0), IDM_CHINESE,
        L"\x4e2d\x6587\x8f93\x5165");
    AppendMenuW(
        menu,
        MF_STRING | (!langBarChineseMode() ? MF_CHECKED : 0), IDM_ENGLISH,
        L"\x82f1\x6587\x8f93\x5165\xFF08\x76f4\x63a5\x952e\x5165\xFF09");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS_GUI, L"Fcitx5 \x8BBE\x7F6E\x2026");
    AppendMenuW(menu, MF_STRING, IDM_OPEN_CONFIG_DIR,
                L"\x6253\x5F00\x7528\x6237\x914D\x7F6E\x6587\x4EF6\x5939");
    SetForegroundWindow(owner);
    const UINT cmd =
        TrackPopupMenu(menu,
                     TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                     pt.x, pt.y, 0, owner, nullptr);
    DestroyMenu(menu);
    PostMessageW(owner, WM_NULL, 0, 0);
    switch (cmd) {
    case IDM_CHINESE:
        langBarScheduleSetChineseMode(true);
        break;
    case IDM_ENGLISH:
        langBarScheduleSetChineseMode(false);
        break;
    case IDM_SETTINGS_GUI:
        launchSettingsGui();
        break;
    case IDM_OPEN_CONFIG_DIR:
        exploreUserFcitxConfig();
        break;
    default:
        break;
    }
}

} // namespace fcitx
