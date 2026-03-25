#include "tsf.h"
#include "LangBarTray.h"
#include "../dll/util.h"

#include <fcitx-utils/log.h>
#include <filesystem>
#include <fstream>
#include <oleauto.h>
#include <shellapi.h>
#include <shlobj.h>
#include <vector>
#include <string_view>
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
#ifndef NIF_SHOWTIP
#define NIF_SHOWTIP 0x00000080
#endif
#ifndef NOTIFYICON_VERSION_4
#define NOTIFYICON_VERSION_4 4U
#endif
#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif
#ifndef NIN_KEYSELECT
#define NIN_KEYSELECT (WM_USER + 1)
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
    IDM_RIME_DEPLOY,
    IDM_RIME_SYNC,
    IDM_RIME_OPEN_USER_DIR,
    IDM_INPUT_METHOD_BASE = 0x4300,
};

constexpr UINT kShellTrayCallback = WM_APP + 88;
constexpr UINT kShellTrayUid = 1;
/** Win32 resource id in fcitx5-ime.rc (fcitx5-x86_64.dll); keep in sync with .rc.in */
constexpr WORD kFcitxPenguinIconResId = 100;
const wchar_t kShellTrayHostClass[] = L"Fcitx5ShellTrayHost";
bool gShellTrayClassRegistered = false;

bool currentProcessIsExplorer() {
    WCHAR exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        return false;
    }
    const std::wstring_view path(exePath);
    const size_t pos = path.find_last_of(L"\\/");
    const std::wstring_view file =
        pos == std::wstring_view::npos ? path : path.substr(pos + 1);
    return _wcsicmp(std::wstring(file).c_str(), L"explorer.exe") == 0;
}

bool launchDetachedProcess(const std::wstring &application,
                           const std::wstring &arguments,
                           const std::wstring &workingDirectory = {}) {
    std::wstring commandLine = L"\"" + application + L"\"";
    if (!arguments.empty()) {
        commandLine += L" ";
        commandLine += arguments;
    }
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    const wchar_t *cwd =
        workingDirectory.empty() ? nullptr : workingDirectory.c_str();
    const BOOL ok = CreateProcessW(application.c_str(), buffer.data(), nullptr,
                                   nullptr, FALSE, DETACHED_PROCESS, nullptr,
                                   cwd, &si, &pi);
    if (!ok) {
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

std::vector<wchar_t> buildEnvironmentWithPathPrefix(
    const std::wstring &pathPrefix) {
    if (pathPrefix.empty()) {
        return {};
    }
    LPWCH envBlock = GetEnvironmentStringsW();
    if (!envBlock) {
        return {};
    }
    std::vector<std::wstring> entries;
    std::wstring existingPath;
    for (const wchar_t *cursor = envBlock; *cursor;) {
        std::wstring entry(cursor);
        cursor += entry.size() + 1;
        if (entry.rfind(L"PATH=", 0) == 0 || entry.rfind(L"Path=", 0) == 0) {
            const size_t eq = entry.find(L'=');
            existingPath = eq == std::wstring::npos ? L"" : entry.substr(eq + 1);
            continue;
        }
        entries.push_back(std::move(entry));
    }
    FreeEnvironmentStringsW(envBlock);

    std::wstring mergedPath = pathPrefix;
    if (!existingPath.empty()) {
        if (!mergedPath.empty() && mergedPath.back() != L';') {
            mergedPath += L';';
        }
        mergedPath += existingPath;
    }
    entries.push_back(L"PATH=" + mergedPath);

    size_t totalChars = 1;
    for (const auto &entry : entries) {
        totalChars += entry.size() + 1;
    }
    std::vector<wchar_t> block;
    block.reserve(totalChars);
    for (const auto &entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

bool launchDetachedProcessWithPathPrefix(const std::wstring &application,
                                         const std::wstring &arguments,
                                         const std::wstring &workingDirectory,
                                         const std::wstring &pathPrefix) {
    std::wstring commandLine = L"\"" + application + L"\"";
    if (!arguments.empty()) {
        commandLine += L" ";
        commandLine += arguments;
    }
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');
    auto env = buildEnvironmentWithPathPrefix(pathPrefix);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    const wchar_t *cwd =
        workingDirectory.empty() ? nullptr : workingDirectory.c_str();
    const DWORD flags =
        DETACHED_PROCESS | (env.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT);
    const BOOL ok = CreateProcessW(application.c_str(), buffer.data(), nullptr,
                                   nullptr, FALSE, flags,
                                   env.empty() ? nullptr : env.data(), cwd, &si,
                                   &pi);
    if (!ok) {
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void openDirectoryInDetachedExplorer(const std::wstring &dir) {
    if (dir.empty()) {
        return;
    }
    if (currentProcessIsExplorer()) {
        launchDetachedProcess(L"C:\\Windows\\explorer.exe", L"\"" + dir + L"\"");
        return;
    }
    ShellExecuteW(nullptr, L"explore", dir.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
}

std::filesystem::path sharedTrayInputMethodRequestFile() {
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return {};
    }
    return std::filesystem::path(appData) / L"Fcitx5" /
           L"pending-tray-input-method.txt";
}

std::string readSharedTrayInputMethodRequestFile() {
    const auto path = sharedTrayInputMethodRequestFile();
    if (path.empty()) {
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::string value;
    std::getline(in, value);
    while (!value.empty() &&
           (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' ||
            value.back() == '\t')) {
        value.pop_back();
    }
    return value;
}

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
    nid.uFlags |= NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
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
    const std::wstring exeStr = exe.wstring();
    if (currentProcessIsExplorer()) {
        launchDetachedProcess(exeStr, L"", exe.parent_path().wstring());
        return;
    }
    ShellExecuteW(nullptr, L"open", exeStr.c_str(), nullptr,
                  exe.parent_path().c_str(), SW_SHOWNORMAL);
}

std::filesystem::path fcitxPortableRoot() {
    WCHAR dllPath[MAX_PATH];
    if (!GetModuleFileNameW(dllInstance, dllPath, MAX_PATH)) {
        return {};
    }
    std::filesystem::path dll(dllPath);
    return dll.parent_path().parent_path();
}

std::filesystem::path userRimeConfigPath() {
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return {};
    }
    return std::filesystem::path(appData) / L"Fcitx5" / L"rime";
}

std::filesystem::path locateRimeDeployer() {
    const auto portableRoot = fcitxPortableRoot();
    const std::filesystem::path candidates[] = {
        portableRoot / L"bin" / L"rime_deployer.exe",
        portableRoot / L"rime_deployer.exe",
        L"C:\\msys64\\clang64\\bin\\rime_deployer.exe",
    };
    for (const auto &candidate : candidates) {
        std::error_code ec;
        if (!candidate.empty() && std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

bool launchRimeDeploy() {
    const auto deployer = locateRimeDeployer();
    if (deployer.empty()) {
        tsfTrace("rime deploy tool not found");
        FCITX_WARN() << "Rime deploy tool not found.";
        return false;
    }
    const auto portableRoot = fcitxPortableRoot();
    const auto userDir = userRimeConfigPath();
    if (portableRoot.empty() || userDir.empty()) {
        tsfTrace("rime deploy aborted missing root/user dir");
        FCITX_WARN() << "Rime deploy aborted: missing portable root or user dir.";
        return false;
    }
    const auto sharedDir = portableRoot / L"share" / L"rime-data";
    const auto stagingDir = userDir / L"build";
    std::error_code ec;
    std::filesystem::create_directories(userDir, ec);
    ec.clear();
    std::filesystem::create_directories(stagingDir, ec);
    const std::wstring args = L"--build \"" + userDir.wstring() + L"\" \"" +
                              sharedDir.wstring() + L"\" \"" +
                              stagingDir.wstring() + L"\"";
    tsfTrace("launch rime deployer exe=" + deployer.string());
    const auto portableBin = (portableRoot / L"bin").wstring();
    const bool ok = launchDetachedProcessWithPathPrefix(
        deployer.wstring(), args, deployer.parent_path().wstring(), portableBin);
    if (!ok) {
        tsfTrace("launch rime deployer failed");
        FCITX_WARN() << "Failed to launch Rime deployer: " << deployer.string();
        return false;
    }
    FCITX_INFO() << "Launched Rime deployer: " << deployer.string();
    return true;
}

void exploreUserFcitxConfig() {
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return;
    }
    std::wstring dir = appData;
    dir += L"\\Fcitx5";
    openDirectoryInDetachedExplorer(dir);
}

void exploreUserRimeConfig() {
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return;
    }
    std::wstring dir = appData;
    dir += L"\\Fcitx5\\rime";
    CreateDirectoryW(dir.c_str(), nullptr);
    openDirectoryInDetachedExplorer(dir);
}

std::wstring trayInputMethodMenuText(const ProfileInputMethodItem &item) {
    if (item.uniqueName == "pinyin") {
        return L"拼音";
    }
    if (item.uniqueName == "wbx") {
        return L"五笔字型";
    }
    if (item.uniqueName == "wubi98") {
        return L"五笔98";
    }
    if (item.uniqueName == "chewing") {
        return L"新酷音";
    }
    if (item.uniqueName == "rime") {
        return L"中州韵";
    }
    if (!item.displayName.empty()) {
        return item.displayName;
    }
    return std::wstring(item.uniqueName.begin(), item.uniqueName.end());
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
    // SHOWNINTRAY helps some hosts surface the lang bar item; shell icon still
    // provides a taskbar entry when the lang bar is hidden.
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
        tsf_->showShellTrayContextMenuAt(pt, owner);
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

void Tsf::langBarScheduleActivateInputMethod(const std::string &uniqueName) {
    if (uniqueName.empty() || !engine_) {
        return;
    }
    persistSharedTrayInputMethodRequest(uniqueName);
    pendingTrayInputMethod_ = uniqueName;
    pendingTrayInputMethodFromSharedRequest_ = false;
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
        const auto im = pendingTrayInputMethod_;
        pendingTrayInputMethod_.clear();
        chineseActive_ = true;
        candidateWin_.hide();
        endCandidateListUiElement();
        engine_->clear();
        engine_->activateProfileInputMethod(im);
        langBarNotifyIconUpdate();
    }
}

void Tsf::persistSharedTrayInputMethodRequest(
    const std::string &uniqueName) const {
    const auto path = sharedTrayInputMethodRequestFile();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    out << uniqueName;
}

void Tsf::clearSharedTrayInputMethodRequest() const {
    const auto path = sharedTrayInputMethodRequestFile();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

bool Tsf::sharedTrayInputMethodRequestPending() const {
    return !readSharedTrayInputMethodRequestFile().empty();
}

bool Tsf::scheduleSharedTrayInputMethodRequest(ITfContext *preferredContext) {
    if (!engine_ || !pendingTrayInputMethod_.empty()) {
        tsfTrace("scheduleSharedTrayInputMethodRequest skipped engine/pending");
        FCITX_DEBUG() << "scheduleSharedTrayInputMethodRequest skipped"
                      << " engine=" << (engine_ != nullptr)
                      << " pending=" << pendingTrayInputMethod_
                      << " pid=" << GetCurrentProcessId();
        return false;
    }
    if (currentProcessIsExplorer()) {
        tsfTrace("scheduleSharedTrayInputMethodRequest skipped in explorer");
        FCITX_DEBUG() << "scheduleSharedTrayInputMethodRequest skipped in explorer"
                      << " pid=" << GetCurrentProcessId();
        return false;
    }
    const auto uniqueName = readSharedTrayInputMethodRequestFile();
    if (uniqueName.empty()) {
        tsfTrace("scheduleSharedTrayInputMethodRequest no shared request");
        FCITX_DEBUG() << "scheduleSharedTrayInputMethodRequest no shared request"
                      << " pid=" << GetCurrentProcessId();
        return false;
    }
    tsfTrace("scheduleSharedTrayInputMethodRequest consume target=" + uniqueName +
             " current=" + engine_->currentInputMethod());
    FCITX_INFO() << "scheduleSharedTrayInputMethodRequest consume request target="
                 << uniqueName << " current=" << engine_->currentInputMethod()
                 << " pid=" << GetCurrentProcessId();
    pendingTrayInputMethod_ = uniqueName;
    pendingTrayInputMethodFromSharedRequest_ = true;
    HRESULT hr = E_FAIL;
    ComPtr<ITfContext> ctx;
    if (preferredContext) {
        ctx = preferredContext;
    } else {
        ctx = textEditSinkContext_;
    }
    if (!ctx && threadMgr_) {
        ComPtr<ITfDocumentMgr> dm;
        if (SUCCEEDED(threadMgr_->GetFocus(dm.ReleaseAndGetAddressOf())) && dm) {
            dm->GetTop(ctx.ReleaseAndGetAddressOf());
        }
    }
    if (ctx) {
        ctx->RequestEditSession(clientId_, this, TF_ES_SYNC | TF_ES_READWRITE,
                                &hr);
    }
    if (FAILED(hr)) {
        tsfTrace("scheduleSharedTrayInputMethodRequest RequestEditSession failed target=" +
                 uniqueName);
        FCITX_WARN() << "scheduleSharedTrayInputMethodRequest edit session failed target="
                     << uniqueName << " hr=0x" << std::hex
                     << static_cast<unsigned long>(hr) << std::dec
                     << " pid=" << GetCurrentProcessId();
        pendingTrayInputMethod_.clear();
        pendingTrayInputMethodFromSharedRequest_ = false;
        return false;
    }
    tsfTrace("scheduleSharedTrayInputMethodRequest RequestEditSession success target=" +
             uniqueName);
    FCITX_INFO() << "scheduleSharedTrayInputMethodRequest edit session scheduled target="
                 << uniqueName << " pid=" << GetCurrentProcessId();
    return true;
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
        const UINT code = LOWORD(static_cast<DWORD_PTR>(lp));
        if (code == WM_LBUTTONUP || code == NIN_SELECT ||
            code == NIN_KEYSELECT) {
            self->langBarScheduleToggleChinese();
        } else if (code == WM_RBUTTONUP || code == WM_CONTEXTMENU) {
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
    nid.uFlags |= NIF_TIP | NIF_SHOWTIP;
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
    showShellTrayContextMenuAt(pt, owner);
}

void Tsf::showShellTrayContextMenuAt(POINT pt, HWND owner) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    const auto profileItems =
        engine_ ? engine_->profileInputMethods() : std::vector<ProfileInputMethodItem>{};
    const bool currentIsRime =
        engine_ && engine_->currentInputMethod() == "rime";
    AppendMenuW(menu, MF_STRING | (langBarChineseMode() ? MF_CHECKED : 0),
                IDM_CHINESE, L"中文模式");
    AppendMenuW(menu,
                MF_STRING | (!langBarChineseMode() ? MF_CHECKED : 0),
                IDM_ENGLISH, L"英文模式（直接键入）");
    HMENU imMenu = CreatePopupMenu();
    if (imMenu) {
        if (profileItems.empty()) {
            AppendMenuW(imMenu, MF_STRING | MF_GRAYED, IDM_INPUT_METHOD_BASE,
                        L"当前 profile 中没有可切换输入法");
        } else {
            for (size_t i = 0; i < profileItems.size(); ++i) {
                AppendMenuW(
                    imMenu,
                    MF_STRING |
                        (profileItems[i].isCurrent ? MF_CHECKED : 0),
                    IDM_INPUT_METHOD_BASE + static_cast<UINT>(i),
                    trayInputMethodMenuText(profileItems[i]).c_str());
            }
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(imMenu),
                    L"切换输入法");
    }
    if (currentIsRime) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_RIME_DEPLOY, L"重新部署中州韵");
        AppendMenuW(menu, MF_STRING, IDM_RIME_SYNC, L"同步中州韵用户数据");
        AppendMenuW(menu, MF_STRING, IDM_RIME_OPEN_USER_DIR,
                    L"打开中州韵配置目录");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS_GUI, L"打开设置界面…");
    AppendMenuW(menu, MF_STRING, IDM_OPEN_CONFIG_DIR, L"打开配置文件夹");
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
    case IDM_RIME_DEPLOY:
        if (!launchRimeDeploy() && engine_) {
            engine_->invokeInputMethodSubConfig("rime", "deploy");
        }
        break;
    case IDM_RIME_SYNC:
        if (engine_) {
            engine_->invokeInputMethodSubConfig("rime", "sync");
        }
        break;
    case IDM_RIME_OPEN_USER_DIR:
        exploreUserRimeConfig();
        break;
    default:
        if (cmd >= IDM_INPUT_METHOD_BASE &&
            cmd < IDM_INPUT_METHOD_BASE + profileItems.size()) {
            langBarScheduleActivateInputMethod(
                profileItems[cmd - IDM_INPUT_METHOD_BASE].uniqueName);
        }
        break;
    }
}

} // namespace fcitx
