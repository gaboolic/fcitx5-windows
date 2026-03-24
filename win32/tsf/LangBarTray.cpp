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

extern void DllAddRef();
extern void DllRelease();

namespace fcitx {
extern HINSTANCE dllInstance;

namespace {

const GUID kFcitxTrayLangBarItemId = {
    0xf7e8d9c0, 0xb1a2, 0x4e3f, {0x9d, 0x8c, 0x7e, 0x6f, 0x5a, 0x4b, 0x3c, 0x2d}};

enum TrayMenuId : UINT {
    IDM_CHINESE = 0x4100,
    IDM_ENGLISH,
    IDM_SETTINGS_GUI,
    IDM_OPEN_CONFIG_DIR,
};

HICON loadPenguinIconNearDll(unsigned cx, unsigned cy) {
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
    pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_BTN_MENU |
                     TF_LBI_STYLE_SHOWNINTRAY;
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
        tsf_ && tsf_->langBarChineseMode() ? L"Fcitx5 — 中文输入\n左键：中/英\n右键：菜单"
                                            : L"Fcitx5 — English (pass-through)\n左键：中/英\n右键：菜单";
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
            if (!tsf_->langBarChineseMode()) {
                tsf_->langBarScheduleToggleChinese();
            }
            break;
        case IDM_ENGLISH:
            if (tsf_->langBarChineseMode()) {
                tsf_->langBarScheduleToggleChinese();
            }
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
        return langBarItem_ != nullptr;
    }
    ComPtr<ITfLangBarItemMgr> mgr;
    if (FAILED(threadMgr_->QueryInterface(
            IID_ITfLangBarItemMgr,
            reinterpret_cast<void **>(mgr.ReleaseAndGetAddressOf())))) {
        return false;
    }
    auto *btn = new FcitxLangBarButton(this);
    if (FAILED(mgr->AddItem(btn))) {
        btn->Release();
        return false;
    }
    langBarItem_ = btn;
    return true;
}

void Tsf::uninitLangBarTrayItem() {
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
    if (textEditSinkContext_) {
        textEditSinkContext_->RequestEditSession(clientId_, this,
                                                  TF_ES_SYNC | TF_ES_READWRITE,
                                                  &hr);
    }
    if (FAILED(hr)) {
        pendingTrayToggleChinese_ = false;
        trayToggleChineseWithoutContext();
    }
}

void Tsf::langBarNotifyIconUpdate() {
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
    langBarNotifyIconUpdate();
}

} // namespace fcitx
