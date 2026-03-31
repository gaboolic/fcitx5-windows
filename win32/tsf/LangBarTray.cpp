#include "LangBarTray.h"
#include "../dll/util.h"
#include "TrayServiceIpc.h"
#include "tsf.h"

#include <atomic>
#include "TsfStubLog.h"
#include <filesystem>
#include <iterator>
#include <fstream>
#include <memory>
#include <windows.h>
#include <oleauto.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string_view>
#include <unordered_map>
#include <vector>

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

class ScopedDllPin {
  public:
    ScopedDllPin() { DllAddRef(); }
    ~ScopedDllPin() { DllRelease(); }

    ScopedDllPin(const ScopedDllPin &) = delete;
    ScopedDllPin &operator=(const ScopedDllPin &) = delete;
};

const GUID kFcitxTrayLangBarItemId = {
    0xf7e8d9c0,
    0xb1a2,
    0x4e3f,
    {0x9d, 0x8c, 0x7e, 0x6f, 0x5a, 0x4b, 0x3c, 0x2d}};

/** Stable id for Shell_NotifyIcon so Windows can persist tray visibility per
 * user. */
const GUID kFcitxShellTrayNotifyGuid = {
    0x8b4d3a2f,
    0x1e0c,
    0x4a5b,
    {0x9c, 0x8d, 0x7e, 0x6f, 0x5a, 0x4b, 0x3c, 0x2e}};

enum TrayMenuId : UINT {
    IDM_INPUT_MODE_CHINESE = 0x4100,
    IDM_INPUT_MODE_ENGLISH,
    IDM_FULLWIDTH_ON,
    IDM_FULLWIDTH_OFF,
    IDM_SCRIPT_TRADITIONAL,
    IDM_SCRIPT_SIMPLIFIED,
    IDM_PUNCTUATION_CHINESE,
    IDM_PUNCTUATION_ENGLISH,
    IDM_SETTINGS_GUI,
    IDM_OPEN_CONFIG_DIR,
    IDM_OPEN_LOG_DIR,
    IDM_RIME_DEPLOY,
    IDM_RIME_SYNC,
    IDM_RIME_OPEN_USER_DIR,
    IDM_RIME_OPEN_LOG_DIR,
    IDM_STATUS_ACTION_BASE = 0x4200,
    IDM_INPUT_METHOD_BASE = 0x4300,
    IDM_SHUANGPIN_SCHEME_BASE = 0x4400,
};

struct ShuangpinSchemeOption {
    const char *value;
    const wchar_t *menuText;
};

constexpr const wchar_t kPinyinConfigRelativePath[] =
    L"config\\fcitx5\\conf\\pinyin.conf";
constexpr ShuangpinSchemeOption kShuangpinSchemes[] = {
    {"Ziranma", L"自然码 (Ziranma)"},
    {"MS", L"微软双拼 (MS)"},
    {"Xiaohe", L"小鹤双拼 (Xiaohe)"},
    {"Ziguang", L"紫光双拼 (Ziguang)"},
    {"ABC", L"智能 ABC 双拼 (ABC)"},
    {"Zhongwenzhixing", L"中文之星 (Zhongwenzhixing)"},
    {"PinyinJiajia", L"拼音加加 (PinyinJiajia)"},
    {"Custom", L"自定义 (Custom)"},
};

constexpr UINT kShellTrayCallback = WM_APP + 88;
constexpr UINT kShellTrayUid = 1;
constexpr UINT_PTR kShellTrayRetryTimerId = 0x4654;
constexpr UINT kShellTrayRetryDelayMs = 1500;
const wchar_t kStandaloneTrayHelperWindowClass[] =
    L"Fcitx5StandaloneTrayHelperWindow";
const wchar_t kStandaloneTrayHelperExeName[] = L"fcitx5-tray-helper.exe";
/** Win32 resource id in fcitx5-ime.rc (fcitx5-x86_64.dll); keep in sync with
 * .rc.in */
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
    return fcitx::wideStringCompareI(std::wstring(file).c_str(),
                                     L"explorer.exe") == 0;
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
    const BOOL ok =
        CreateProcessW(application.c_str(), buffer.data(), nullptr, nullptr,
                       FALSE, DETACHED_PROCESS, nullptr, cwd, &si, &pi);
    if (!ok) {
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool standaloneTrayHelperWindowExists() {
    return FindWindowW(kStandaloneTrayHelperWindowClass, nullptr) != nullptr;
}

std::filesystem::path locateStandaloneTrayHelperExe() {
    WCHAR dllPath[MAX_PATH] = {};
    if (!GetModuleFileNameW(dllInstance, dllPath, MAX_PATH)) {
        return {};
    }
    std::filesystem::path dll(dllPath);
    return dll.parent_path() / kStandaloneTrayHelperExeName;
}

bool ensureStandaloneTrayHelperRunning() {
    if (standaloneTrayHelperWindowExists()) {
        return true;
    }
    const auto exe = locateStandaloneTrayHelperExe();
    std::error_code ec;
    if (exe.empty() || !std::filesystem::exists(exe, ec)) {
        tsfTrace("ensureStandaloneTrayHelperRunning missing exe path=" +
                 exe.string());
        return false;
    }
    const bool launched =
        launchDetachedProcess(exe.wstring(), L"", exe.parent_path().wstring());
    tsfTrace(std::string("ensureStandaloneTrayHelperRunning launched=") +
             (launched ? "true" : "false") + " path=" + exe.string());
    return launched || standaloneTrayHelperWindowExists();
}

HWND standaloneTrayHelperWindow() {
    HWND hwnd = fcitx::findStandaloneTrayHelperWindow();
    if (hwnd) {
        return hwnd;
    }
    for (int attempt = 0; attempt < 10; ++attempt) {
        Sleep(20);
        hwnd = fcitx::findStandaloneTrayHelperWindow();
        if (hwnd) {
            return hwnd;
        }
    }
    return nullptr;
}

std::vector<wchar_t>
buildEnvironmentWithPathPrefix(const std::wstring &pathPrefix,
                               const std::wstring &glogLogDir = {}) {
    if (pathPrefix.empty() && glogLogDir.empty()) {
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
            existingPath =
                eq == std::wstring::npos ? L"" : entry.substr(eq + 1);
            continue;
        }
        if (entry.rfind(L"GLOG_log_dir=", 0) == 0) {
            continue;
        }
        entries.push_back(std::move(entry));
    }
    FreeEnvironmentStringsW(envBlock);

    if (!pathPrefix.empty()) {
        std::wstring mergedPath = pathPrefix;
        if (!existingPath.empty()) {
            if (!mergedPath.empty() && mergedPath.back() != L';') {
                mergedPath += L';';
            }
            mergedPath += existingPath;
        }
        entries.push_back(L"PATH=" + mergedPath);
    } else if (!existingPath.empty()) {
        entries.push_back(L"PATH=" + existingPath);
    }
    if (!glogLogDir.empty()) {
        entries.push_back(L"GLOG_log_dir=" + glogLogDir);
    }

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
    const BOOL ok = CreateProcessW(
        application.c_str(), buffer.data(), nullptr, nullptr, FALSE, flags,
        env.empty() ? nullptr : env.data(), cwd, &si, &pi);
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
    // 使用 ShellExecuteExW 打开目录，避免在资源管理器进程中出现问题
    // SEE_MASK_NOASYNC 确保操作同步完成，避免资源管理器崩溃
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

std::filesystem::path sharedTrayInputMethodRequestFile() {
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return {};
    }
    return std::filesystem::path(appData) / L"Fcitx5" /
           L"pending-tray-input-method.txt";
}

std::filesystem::path sharedTrayCurrentInputMethodFile() {
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return {};
    }
    return std::filesystem::path(appData) / L"Fcitx5" /
           L"current-tray-input-method.txt";
}

std::filesystem::path sharedTrayChineseModeRequestFile() {
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return {};
    }
    return std::filesystem::path(appData) / L"Fcitx5" /
           L"pending-tray-chinese-mode.txt";
}

std::filesystem::path sharedTrayChineseModeStateFile() {
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return {};
    }
    return std::filesystem::path(appData) / L"Fcitx5" /
           L"current-tray-chinese-mode.txt";
}

std::filesystem::path sharedTrayStatusActionRequestFile() {
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return {};
    }
    return std::filesystem::path(appData) / L"Fcitx5" /
           L"pending-tray-status-action.txt";
}

std::filesystem::path sharedTrayPinyinReloadRequestFile() {
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return {};
    }
    return std::filesystem::path(appData) / L"Fcitx5" /
           L"pending-tray-pinyin-reload.txt";
}

std::filesystem::path sharedTrayStatusActionStateFile() {
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return {};
    }
    return std::filesystem::path(appData) / L"Fcitx5" /
           L"current-tray-status-actions.txt";
}

std::filesystem::path fcitx5LogDir();
std::filesystem::path fcitxPortableRoot();

std::string trimSharedTrayValue(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' ||
                              value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    size_t first = 0;
    while (first < value.size() &&
           (value[first] == ' ' || value[first] == '\t' ||
            value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }
    if (first > 0) {
        value.erase(0, first);
    }
    return value;
}

std::string wideToUtf8(const std::wstring &wide) {
    if (wide.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                      static_cast<int>(wide.size()), nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string utf8(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        utf8.data(), n, nullptr, nullptr);
    return utf8;
}

std::string readSharedTrayTextFile(const std::filesystem::path &path) {
    if (path.empty()) {
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::string value;
    std::getline(in, value);
    return trimSharedTrayValue(std::move(value));
}

void writeSharedTrayTextFile(const std::filesystem::path &path,
                             const std::string &value) {
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    out << value;
}

bool readSharedTrayChineseModeFile(const std::filesystem::path &path,
                                   bool *value) {
    if (!value) {
        return false;
    }
    const auto text = readSharedTrayTextFile(path);
    if (text == "1" || text == "true" || text == "chinese") {
        *value = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "english") {
        *value = false;
        return true;
    }
    return false;
}

void writeSharedTrayChineseModeFile(const std::filesystem::path &path,
                                    bool value) {
    writeSharedTrayTextFile(path, value ? "1" : "0");
}

std::string readSharedTrayInputMethodRequestFile() {
    return readSharedTrayTextFile(sharedTrayInputMethodRequestFile());
}

std::string readSharedTrayStatusActionRequestFile() {
    return readSharedTrayTextFile(sharedTrayStatusActionRequestFile());
}

bool readSharedTrayPinyinReloadRequestFile() {
    return readSharedTrayTextFile(sharedTrayPinyinReloadRequestFile()) == "1";
}

void persistSharedTrayCurrentInputMethodState(const std::string &uniqueName) {
    if (!uniqueName.empty()) {
        writeSharedTrayTextFile(sharedTrayCurrentInputMethodFile(), uniqueName);
    }
}

std::string readSharedTrayCurrentInputMethodState() {
    auto current = readSharedTrayTextFile(sharedTrayCurrentInputMethodFile());
    if (current.empty()) {
        current = readSharedTrayInputMethodRequestFile();
    }
    return current;
}

void persistSharedTrayChineseModeState(bool chineseMode) {
    writeSharedTrayChineseModeFile(sharedTrayChineseModeStateFile(),
                                   chineseMode);
}

bool readSharedTrayChineseModeState(bool *value) {
    return readSharedTrayChineseModeFile(sharedTrayChineseModeStateFile(),
                                         value);
}

void persistSharedTrayChineseModeRequest(bool chineseMode) {
    writeSharedTrayChineseModeFile(sharedTrayChineseModeRequestFile(),
                                   chineseMode);
}

void persistSharedTrayPinyinReloadRequest() {
    writeSharedTrayTextFile(sharedTrayPinyinReloadRequestFile(), "1");
}

bool readSharedTrayChineseModeRequest(bool *value) {
    return readSharedTrayChineseModeFile(sharedTrayChineseModeRequestFile(),
                                         value);
}

void clearSharedTrayChineseModeRequest() {
    const auto path = sharedTrayChineseModeRequestFile();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void clearSharedTrayPinyinReloadRequestFile() {
    const auto path = sharedTrayPinyinReloadRequestFile();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void fillShellTrayNidIdentity(NOTIFYICONDATAW *nid, HWND hostHwnd,
                              bool useGuid = true) {
    ZeroMemory(nid, sizeof(*nid));
    nid->cbSize = sizeof(*nid);
    nid->hWnd = hostHwnd;
    nid->uID = kShellTrayUid;
    nid->uFlags = 0;
    if (useGuid) {
        nid->uFlags |= NIF_GUID;
        nid->guidItem = kFcitxShellTrayNotifyGuid;
    }
}

void showTrayBalloon(HWND hostHwnd, const wchar_t *title, const wchar_t *text,
                     DWORD infoFlags = NIIF_INFO) {
    if (!hostHwnd) {
        return;
    }
    NOTIFYICONDATAW nid = {};
    fillShellTrayNidIdentity(&nid, hostHwnd, true);
    nid.uFlags |= NIF_INFO;
    nid.dwInfoFlags = infoFlags;
    fcitx::wideStringCopyTruncate(nid.szInfoTitle, std::size(nid.szInfoTitle),
                                  title);
    fcitx::wideStringCopyTruncate(nid.szInfo, std::size(nid.szInfo), text);
    nid.uTimeout = 3000;
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        fillShellTrayNidIdentity(&nid, hostHwnd, false);
        nid.uFlags |= NIF_INFO;
        nid.dwInfoFlags = infoFlags;
        fcitx::wideStringCopyTruncate(nid.szInfoTitle,
                                      std::size(nid.szInfoTitle), title);
        fcitx::wideStringCopyTruncate(nid.szInfo, std::size(nid.szInfo), text);
        nid.uTimeout = 3000;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

bool shellTrayNotifyAdd(HWND hostHwnd, HICON icon, bool chineseMode,
                        bool *usedGuidIdentity) {
    auto populate = [&](NOTIFYICONDATAW *nid, bool useGuid) {
        fillShellTrayNidIdentity(nid, hostHwnd, useGuid);
        nid->uFlags |= NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        nid->uCallbackMessage = kShellTrayCallback;
        nid->hIcon = icon;
        if (chineseMode) {
            fcitx::wideStringCopyTruncate(
                nid->szTip, std::size(nid->szTip),
                L"Fcitx5 \x2014 \x4e2d\x6587\nShift / Ctrl+Space "
                L"\x5207\x6362\x4e2d/\x82f1");
        } else {
            fcitx::wideStringCopyTruncate(
                nid->szTip, std::size(nid->szTip),
                L"Fcitx5 \x2014 English\nShift / Ctrl+Space "
                L"\x5207\x6362\x4e2d/\x82f1");
        }
    };

    NOTIFYICONDATAW nid = {};
    populate(&nid, true);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (Shell_NotifyIconW(NIM_ADD, &nid)) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        if (usedGuidIdentity) {
            *usedGuidIdentity = true;
        }
        return true;
    }
    const DWORD guidError = GetLastError();

    NOTIFYICONDATAW legacy = {};
    populate(&legacy, false);
    Shell_NotifyIconW(NIM_DELETE, &legacy);
    if (Shell_NotifyIconW(NIM_ADD, &legacy)) {
        legacy.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &legacy);
        if (usedGuidIdentity) {
            *usedGuidIdentity = false;
        }
        tsfTrace("shellTrayNotifyAdd fallback to legacy identity after guid "
                 "failure gle=" +
                 std::to_string(static_cast<unsigned long>(guidError)));
        return true;
    }
    const DWORD legacyError = GetLastError();
    tsfTrace("shellTrayNotifyAdd failed guid_gle=" +
             std::to_string(static_cast<unsigned long>(guidError)) +
             " legacy_gle=" +
             std::to_string(static_cast<unsigned long>(legacyError)));
    return false;
}

HICON loadPenguinIconNearDll(unsigned cx, unsigned cy) {
    HICON embedded = reinterpret_cast<HICON>(LoadImageW(
        dllInstance, MAKEINTRESOURCEW(kFcitxPenguinIconResId), IMAGE_ICON,
        static_cast<int>(cx), static_cast<int>(cy), LR_DEFAULTCOLOR));
    if (!embedded) {
        embedded = reinterpret_cast<HICON>(
            LoadImageW(dllInstance, MAKEINTRESOURCEW(kFcitxPenguinIconResId),
                       IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR));
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
    const std::wstring pwide = fcitx::pathAsWide(p);
    return reinterpret_cast<HICON>(
        LoadImageW(nullptr, pwide.c_str(), IMAGE_ICON, static_cast<int>(cx),
                   static_cast<int>(cy), LR_LOADFROMFILE));
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
    const std::wstring exeDir = fcitx::pathAsWide(exe.parent_path());
    ShellExecuteW(nullptr, L"open", exeStr.c_str(), nullptr, exeDir.c_str(),
                  SW_SHOWNORMAL);
}

std::filesystem::path fcitxPortableRoot() {
    WCHAR dllPath[MAX_PATH];
    if (!GetModuleFileNameW(dllInstance, dllPath, MAX_PATH)) {
        return {};
    }
    std::filesystem::path dll(dllPath);
    return dll.parent_path().parent_path();
}

std::filesystem::path sharedTrayProfileFile() {
    const auto portable =
        fcitxPortableRoot() / L"config" / L"fcitx5" / L"profile";
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return portable;
    }
    const auto roaming = std::filesystem::path(appData) / L"Fcitx5" /
                         L"config" / L"fcitx5" / L"profile";
    std::error_code ec;
    if (std::filesystem::exists(roaming, ec)) {
        return roaming;
    }
    if (std::filesystem::exists(portable, ec)) {
        return portable;
    }
    return roaming;
}

std::vector<ProfileInputMethodItem> readProfileInputMethodsFromConfig() {
    const auto profilePath = sharedTrayProfileFile();
    if (profilePath.empty()) {
        return {};
    }
    std::ifstream in(profilePath, std::ios::binary);
    if (!in.is_open()) {
        tsfTrace(
            "readProfileInputMethodsFromConfig failed to open profile path=" +
            profilePath.string());
        return {};
    }
    std::string section;
    std::string defaultIm;
    std::vector<std::string> names;
    std::string line;
    while (std::getline(in, line)) {
        line = trimSharedTrayValue(std::move(line));
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trimSharedTrayValue(line.substr(0, eq));
        const std::string value = trimSharedTrayValue(line.substr(eq + 1));
        if (section == "Groups/0" && key == "DefaultIM") {
            defaultIm = value;
            continue;
        }
        if (section.rfind("Groups/0/Items/", 0) == 0 && key == "Name" &&
            !value.empty()) {
            bool seen = false;
            for (const auto &name : names) {
                if (name == value) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                names.push_back(value);
            }
        }
    }
    std::string current = readSharedTrayCurrentInputMethodState();
    if (current.empty()) {
        current = defaultIm;
    }
    std::vector<ProfileInputMethodItem> items;
    items.reserve(names.size());
    for (const auto &name : names) {
        items.push_back(ProfileInputMethodItem{name, L"", name == current});
    }
    tsfTrace(
        "readProfileInputMethodsFromConfig path=" + profilePath.string() +
        " items=" + std::to_string(static_cast<unsigned long>(items.size())) +
        " current=" + current);
    return items;
}

std::filesystem::path userRimeConfigPath() {
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return {};
    }
    // Fcitx5 的 Rime 配置在 %APPDATA%\Fcitx5\rime 目录
    // 这是 Fcitx5 中州韵输入法的用户数据目录
    return std::filesystem::path(appData) / L"Fcitx5" / L"rime";
}

std::filesystem::path fcitx5RimeUserDir() {
    // 获取 Fcitx5 的 Rime 用户目录
    // 首先检查环境变量，如果没有设置则使用默认路径
    const std::wstring envDir =
        fcitx::getEnvironmentVariableWide(L"FCITX_RIME_USER_DIR");
    if (!envDir.empty()) {
        return envDir;
    }

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

struct RimeDeployMonitorData {
    HANDLE hProcess;
    HANDLE hThread;
    HWND trayHwnd;
    HANDLE hWaitHandle;
};

static std::unordered_map<HANDLE, std::unique_ptr<RimeDeployMonitorData>>
    g_rimeDeployMonitors;

static void CALLBACK rimeDeployWaitCallback(PVOID lpParameter,
                                            BOOLEAN TimerOrWaitFired) {
    auto it = g_rimeDeployMonitors.find(lpParameter);
    if (it == g_rimeDeployMonitors.end() || !it->second) {
        return;
    }

    auto &data = it->second;

    // 获取退出码
    DWORD exitCode = 0;
    BOOL gotExitCode = GetExitCodeProcess(data->hProcess, &exitCode);

    // 清理资源
    if (data->hWaitHandle) {
        UnregisterWait(data->hWaitHandle);
    }
    CloseHandle(data->hThread);
    CloseHandle(data->hProcess);

    // 显示结果通知
    if (data->trayHwnd) {
        if (gotExitCode && exitCode == 0) {
            showTrayBalloon(data->trayHwnd, L"中州韵部署", L"部署成功完成！",
                            NIIF_INFO);
        } else {
            std::wstring msg =
                L"部署失败 (退出码: " + std::to_wstring(exitCode) + L")";
            showTrayBalloon(data->trayHwnd, L"中州韵部署", msg.c_str(),
                            NIIF_ERROR);
        }
    }

    g_rimeDeployMonitors.erase(it);
}

bool launchRimeDeployWithNotification(HWND trayHwnd) {
    const auto deployer = locateRimeDeployer();
    if (deployer.empty()) {
        tsfTrace("rime deploy tool not found");
        FCITX_WARN() << "Rime deploy tool not found.";
        if (trayHwnd) {
            showTrayBalloon(trayHwnd, L"中州韵部署",
                            L"错误：找不到 rime_deployer.exe", NIIF_ERROR);
        }
        return false;
    }
    const auto portableRoot = fcitxPortableRoot();
    const auto userDir = fcitx5RimeUserDir();
    if (portableRoot.empty() || userDir.empty()) {
        tsfTrace("rime deploy aborted missing root/user dir");
        FCITX_WARN()
            << "Rime deploy aborted: missing portable root or user dir.";
        if (trayHwnd) {
            showTrayBalloon(trayHwnd, L"中州韵部署", L"错误：无法获取配置目录",
                            NIIF_ERROR);
        }
        return false;
    }
    const auto sharedDir = portableRoot / L"share" / L"rime-data";
    const auto stagingDir = userDir / L"build";

    // 调试信息
    FCITX_INFO() << "Rime deploy paths:";
    FCITX_INFO() << "  portableRoot: " << portableRoot.string();
    FCITX_INFO() << "  userDir: " << userDir.string();
    FCITX_INFO() << "  sharedDir: " << sharedDir.string();
    FCITX_INFO() << "  stagingDir: " << stagingDir.string();
    FCITX_INFO() << "  deployer: " << deployer.string();

    // 检查目录是否存在
    std::error_code ec;
    bool userDirExists = std::filesystem::exists(userDir, ec);
    bool sharedDirExists = std::filesystem::exists(sharedDir, ec);
    FCITX_INFO() << "  userDir exists: " << userDirExists;
    FCITX_INFO() << "  sharedDir exists: " << sharedDirExists;

    std::filesystem::create_directories(userDir, ec);
    ec.clear();
    std::filesystem::create_directories(stagingDir, ec);
    const std::wstring args = L"--build \"" + userDir.wstring() + L"\" \"" +
                              sharedDir.wstring() + L"\" \"" +
                              stagingDir.wstring() + L"\"";
    tsfTrace("launch rime deployer exe=" + deployer.string());
    FCITX_INFO() << "Rime deploy command args: "
                 << std::string(args.begin(), args.end());

    // 显示开始部署通知
    if (trayHwnd) {
        showTrayBalloon(trayHwnd, L"中州韵部署", L"正在部署中州韵...",
                        NIIF_INFO);
    }

    // 构建命令行（包含程序名和参数）
    std::wstring commandLine = L"\"" + deployer.wstring() + L"\"";
    if (!args.empty()) {
        commandLine += L" " + args;
    }
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');

    auto env = buildEnvironmentWithPathPrefix((portableRoot / L"bin").wstring(),
                                              fcitx5LogDir().wstring());
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    // 修复：使用局部变量存储工作目录，避免悬空指针
    std::wstring workingDir = deployer.parent_path().wstring();
    const wchar_t *cwd = workingDir.empty() ? nullptr : workingDir.c_str();

    const DWORD flags =
        CREATE_NO_WINDOW | (env.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT);

    // lpApplicationName 为 nullptr，让系统从 lpCommandLine 解析程序名
    const BOOL ok =
        CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, flags,
                       env.empty() ? nullptr : env.data(), cwd, &si, &pi);
    if (!ok) {
        tsfTrace("launch rime deployer failed");
        FCITX_WARN() << "Failed to launch Rime deployer: " << deployer.string();
        if (trayHwnd) {
            showTrayBalloon(trayHwnd, L"中州韵部署", L"错误：启动部署工具失败",
                            NIIF_ERROR);
        }
        return false;
    }

    FCITX_INFO() << "Launched Rime deployer: " << deployer.string();

    // 使用线程池等待机制监视进程，避免创建新线程
    if (trayHwnd) {
        auto monitorData = std::make_unique<RimeDeployMonitorData>();
        monitorData->hProcess = pi.hProcess;
        monitorData->hThread = pi.hThread;
        monitorData->trayHwnd = trayHwnd;
        monitorData->hWaitHandle = nullptr;

        HANDLE hProcessForKey = pi.hProcess;
        g_rimeDeployMonitors[hProcessForKey] = std::move(monitorData);

        // 注册线程池等待，进程结束时自动回调
        if (!RegisterWaitForSingleObject(
                &g_rimeDeployMonitors[hProcessForKey]->hWaitHandle, pi.hProcess,
                rimeDeployWaitCallback, hProcessForKey, INFINITE,
                WT_EXECUTEONLYONCE)) {
            // 注册失败，直接关闭句柄
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            g_rimeDeployMonitors.erase(hProcessForKey);
        }
    } else {
        // 不需要通知时，直接关闭句柄
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    return true;
}

// 保持向后兼容的函数
bool launchRimeDeploy() { return launchRimeDeployWithNotification(nullptr); }

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

bool directoryHasEntries(const std::filesystem::path &dir) {
    std::error_code ec;
    return std::filesystem::exists(dir, ec) &&
           std::filesystem::is_directory(dir, ec) &&
           !std::filesystem::is_empty(dir, ec);
}

std::filesystem::path fcitx5LogDir() {
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return {};
    }
    std::filesystem::path dir =
        std::filesystem::path(appData) / L"Fcitx5" / L"log";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path preferredFcitx5LogDir() {
    const auto logDir = fcitx5LogDir();
    WCHAR appData[MAX_PATH];
    if (logDir.empty() ||
        FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return logDir;
    }
    const auto rootDir = std::filesystem::path(appData) / L"Fcitx5";
    const auto traceFile = rootDir / L"tsf-trace.log";
    std::error_code ec;
    if (!directoryHasEntries(logDir) &&
        std::filesystem::exists(traceFile, ec)) {
        return rootDir;
    }
    return logDir;
}

void exploreFcitx5LogDir() {
    const auto dir = preferredFcitx5LogDir();
    if (dir.empty()) {
        return;
    }
    tsfTrace("exploreFcitx5LogDir path=" + dir.string());
    openDirectoryInDetachedExplorer(dir.wstring());
}

void exploreRimeLogDir() {
    auto dir = fcitx5RimeUserDir();
    if (dir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto sharedLogDir = fcitx5LogDir();
    if (directoryHasEntries(sharedLogDir)) {
        dir = sharedLogDir;
    }
    tsfTrace("exploreRimeLogDir path=" + dir.string());
    openDirectoryInDetachedExplorer(dir.wstring());
}

std::wstring trayInputMethodMenuText(const ProfileInputMethodItem &item) {
    if (item.uniqueName == "pinyin") {
        return L"拼音";
    }
    if (item.uniqueName == "shuangpin") {
        return L"双拼";
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

std::wstring trayChineseModeMenuText(bool chineseMode) {
    return chineseMode ? L"输入模式(中文)" : L"输入模式(英文)";
}

std::wstring trayStatusToggleMenuText(const TrayStatusActionItem &item) {
    if (item.uniqueName == "punctuation") {
        return item.isChecked ? L"中英文标点(中文)" : L"中英文标点(英文)";
    }
    if (item.uniqueName == "fullwidth") {
        return item.isChecked ? L"全角/半角(全角)" : L"全角/半角(半角)";
    }
    if (item.uniqueName == "chttrans") {
        return item.isChecked ? L"字符集(繁体)" : L"字符集(简体)";
    }
    return item.displayName;
}

const TrayStatusActionItem *
findTrayStatusAction(const std::vector<TrayStatusActionItem> &items,
                     const char *uniqueName) {
    for (const auto &item : items) {
        if (item.uniqueName == uniqueName) {
            return &item;
        }
    }
    return nullptr;
}

int shuangpinSchemeIndexFromValue(std::string_view value) {
    for (size_t i = 0;
         i < sizeof(kShuangpinSchemes) / sizeof(kShuangpinSchemes[0]); ++i) {
        if (value == kShuangpinSchemes[i].value) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

std::filesystem::path sharedPinyinConfigFile() {
    const auto portable = fcitxPortableRoot() / kPinyinConfigRelativePath;
    WCHAR appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return portable;
    }
    const auto roaming =
        std::filesystem::path(appData) / L"Fcitx5" / kPinyinConfigRelativePath;
    std::error_code ec;
    if (std::filesystem::exists(roaming, ec)) {
        return roaming;
    }
    if (std::filesystem::exists(portable, ec)) {
        return portable;
    }
    return roaming;
}

std::string loadShuangpinSchemeValue() {
    std::ifstream in(sharedPinyinConfigFile(), std::ios::binary);
    if (!in.is_open()) {
        return kShuangpinSchemes[0].value;
    }
    std::string line;
    while (std::getline(in, line)) {
        line = trimSharedTrayValue(std::move(line));
        if (line.rfind("ShuangpinProfile=", 0) == 0) {
            return trimSharedTrayValue(
                line.substr(sizeof("ShuangpinProfile=") - 1));
        }
    }
    return kShuangpinSchemes[0].value;
}

bool saveShuangpinSchemeValue(std::string_view value) {
    const auto path = sharedPinyinConfigFile();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ifstream in(path, std::ios::binary);
    std::vector<std::string> lines;
    std::string line;
    bool replaced = false;
    bool hasConfigSection = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line == "[Config]") {
            hasConfigSection = true;
        }
        if (line.rfind("ShuangpinProfile=", 0) == 0) {
            line = "ShuangpinProfile=" + std::string(value);
            replaced = true;
        }
        lines.push_back(line);
    }
    in.close();
    if (!replaced) {
        if (!hasConfigSection) {
            lines.push_back("[Config]");
        }
        lines.push_back("ShuangpinProfile=" + std::string(value));
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    for (const auto &entry : lines) {
        out << entry << "\r\n";
    }
    return static_cast<bool>(out);
}

void appendRadioMenuItem(HMENU menu, UINT id, const wchar_t *label) {
    if (!menu || !label) {
        return;
    }
    AppendMenuW(menu, MF_STRING, id, label);
    MENUITEMINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_FTYPE;
    info.fType = MFT_RADIOCHECK;
    SetMenuItemInfoW(menu, id, FALSE, &info);
}

} // namespace

static std::atomic<bool> g_explorerTrayHelperPrimed{false};

bool explorerTrayHelperPrimedOnce() {
    const bool cached =
        g_explorerTrayHelperPrimed.load(std::memory_order_relaxed);
    if (cached && standaloneTrayHelperWindowExists()) {
        tsfTrace("explorerTrayHelperPrimedOnce cached=true helperWindow=true");
        return true;
    }
    if (cached) {
        tsfTrace("explorerTrayHelperPrimedOnce cached=true helperWindow=false; "
                 "retrying helper startup");
    }
    const bool helperReady = ensureStandaloneTrayHelperRunning();
    tsfTrace(std::string("explorerTrayHelperPrimedOnce ensure helperReady=") +
             (helperReady ? "true" : "false"));
    if (!helperReady) {
        return false;
    }
    g_explorerTrayHelperPrimed.store(true, std::memory_order_relaxed);
    return true;
}

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
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfLangBarItem) ||
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
    fcitx::wideStringCopyTruncate(pInfo->szDescription, TF_LBI_DESC_MAXLEN,
                                  L"Fcitx5");
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
            : L"Fcitx5 — English (pass-through)\nShift：中/英  "
              L"Ctrl+Space：中/英\n右键：菜单";
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
        const bool explorer = currentProcessIsExplorer();
        tsfTrace(std::string("FcitxLangBarButton::OnClick right explorer=") +
                 (explorer ? "true" : "false") +
                 " pt=" + std::to_string(static_cast<long long>(pt.x)) + "," +
                 std::to_string(static_cast<long long>(pt.y)));
        if (explorer) {
            tsfTrace("FcitxLangBarButton::OnClick right explorer conservative "
                     "helper-only path");
            tsf_->showShellTrayContextMenu();
            return S_OK;
        }
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
        *phIcon = reinterpret_cast<HICON>(
            LoadImageW(nullptr, MAKEINTRESOURCEW(32512), IMAGE_ICON,
                       static_cast<int>(cx), static_cast<int>(cy), LR_SHARED));
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
    const bool trayReady = initShellTrayIcon();
    if (currentProcessIsExplorer()) {
        tsfTrace(
            std::string("initLangBarTrayItem explorer trayOnly trayReady=") +
            (trayReady ? "true" : "false"));
        return trayReady;
    }
    if (langBarItem_ || !threadMgr_) {
        tsfTrace(std::string("initLangBarTrayItem early return langBarItem=") +
                 (langBarItem_ ? "true" : "false") +
                 " threadMgr=" + (threadMgr_ ? "true" : "false") +
                 " trayReady=" + (trayReady ? "true" : "false"));
        return langBarItem_ != nullptr || trayReady;
    }
    ComPtr<ITfLangBarItemMgr> mgr;
    if (FAILED(threadMgr_->QueryInterface(
            IID_ITfLangBarItemMgr,
            reinterpret_cast<void **>(mgr.ReleaseAndGetAddressOf())))) {
        tsfTrace(
            std::string(
                "initLangBarTrayItem missing ITfLangBarItemMgr trayReady=") +
            (trayReady ? "true" : "false"));
        return trayReady;
    }
    auto *btn = new FcitxLangBarButton(this);
    if (FAILED(mgr->AddItem(btn))) {
        btn->Release();
        tsfTrace(std::string("initLangBarTrayItem AddItem failed trayReady=") +
                 (trayReady ? "true" : "false"));
        return trayReady;
    }
    langBarItem_ = btn;
    tsfTrace(std::string("initLangBarTrayItem success trayReady=") +
             (trayReady ? "true" : "false"));
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
    if (currentProcessIsExplorer()) {
        bool current = chineseActive_;
        readSharedTrayChineseModeState(&current);
        langBarScheduleSetChineseMode(!current);
        return;
    }
    pendingTrayToggleChinese_ = true;
    HRESULT hr = E_FAIL;
    ComPtr<ITfContext> ctx = textEditSinkContext_;
    if (!ctx && threadMgr_) {
        ComPtr<ITfDocumentMgr> dm;
        if (SUCCEEDED(threadMgr_->GetFocus(dm.ReleaseAndGetAddressOf())) &&
            dm) {
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
    if (currentProcessIsExplorer()) {
        chineseActive_ = wantChinese;
        persistSharedTrayChineseModeRequest(wantChinese);
        persistSharedTrayChineseModeState(wantChinese);
        langBarNotifyIconUpdate();
        return;
    }
    if (chineseActive_ == wantChinese) {
        langBarNotifyIconUpdate();
        return;
    }
    langBarScheduleToggleChinese();
}

void Tsf::langBarScheduleActivateInputMethod(const std::string &uniqueName) {
    if (uniqueName.empty()) {
        return;
    }
    persistSharedTrayInputMethodRequest(uniqueName);
    persistSharedTrayCurrentInputMethodState(uniqueName);
    persistSharedTrayChineseModeRequest(true);
    persistSharedTrayChineseModeState(true);
    if (currentProcessIsExplorer() || !engine_) {
        chineseActive_ = true;
        langBarNotifyIconUpdate();
        return;
    }
    pendingTrayInputMethod_ = uniqueName;
    pendingTrayInputMethodFromSharedRequest_ = false;
    HRESULT hr = E_FAIL;
    ComPtr<ITfContext> ctx = textEditSinkContext_;
    if (!ctx && threadMgr_) {
        ComPtr<ITfDocumentMgr> dm;
        if (SUCCEEDED(threadMgr_->GetFocus(dm.ReleaseAndGetAddressOf())) &&
            dm) {
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

void Tsf::clearSharedTrayPinyinReloadRequest() const {
    clearSharedTrayPinyinReloadRequestFile();
}

void Tsf::persistSharedTrayStatusActionState() const {
    const auto path = sharedTrayStatusActionStateFile();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    if (!engine_) {
        return;
    }
    for (const auto &item : engine_->trayStatusActions()) {
        out << item.uniqueName << '\t' << (item.isChecked ? '1' : '0') << '\t'
            << wideToUtf8(item.displayName) << '\n';
    }
}

void Tsf::clearSharedTrayStatusActionRequest() const {
    const auto path = sharedTrayStatusActionRequestFile();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

bool Tsf::sharedTrayChineseModeRequestPending() const {
    bool value = false;
    return readSharedTrayChineseModeRequest(&value);
}

bool Tsf::sharedTrayInputMethodRequestPending() const {
    return !readSharedTrayInputMethodRequestFile().empty();
}

bool Tsf::sharedTrayStatusActionRequestPending() const {
    return !readSharedTrayStatusActionRequestFile().empty();
}

bool Tsf::sharedTrayPinyinReloadRequestPending() const {
    return readSharedTrayPinyinReloadRequestFile();
}

bool Tsf::scheduleSharedTrayChineseModeRequest(ITfContext *preferredContext) {
    if (pendingTraySetChineseModeValid_) {
        return false;
    }
    if (currentProcessIsExplorer()) {
        return false;
    }
    bool wantChinese = false;
    if (!readSharedTrayChineseModeRequest(&wantChinese)) {
        return false;
    }
    if (chineseActive_ == wantChinese) {
        clearSharedTrayChineseModeRequest();
        persistSharedTrayChineseModeState(wantChinese);
        return false;
    }
    pendingTraySetChineseMode_ = wantChinese;
    pendingTraySetChineseModeValid_ = true;
    HRESULT hr = E_FAIL;
    ComPtr<ITfContext> ctx;
    if (preferredContext) {
        ctx = preferredContext;
    } else {
        ctx = textEditSinkContext_;
    }
    if (!ctx && threadMgr_) {
        ComPtr<ITfDocumentMgr> dm;
        if (SUCCEEDED(threadMgr_->GetFocus(dm.ReleaseAndGetAddressOf())) &&
            dm) {
            dm->GetTop(ctx.ReleaseAndGetAddressOf());
        }
    }
    if (ctx) {
        ctx->RequestEditSession(clientId_, this, TF_ES_SYNC | TF_ES_READWRITE,
                                &hr);
    }
    if (FAILED(hr)) {
        pendingTraySetChineseModeValid_ = false;
        if (chineseActive_ != wantChinese) {
            trayToggleChineseWithoutContext();
        }
        clearSharedTrayChineseModeRequest();
        persistSharedTrayChineseModeState(chineseActive_);
        return false;
    }
    return true;
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
        FCITX_DEBUG()
            << "scheduleSharedTrayInputMethodRequest skipped in explorer"
            << " pid=" << GetCurrentProcessId();
        return false;
    }
    const auto uniqueName = readSharedTrayInputMethodRequestFile();
    if (uniqueName.empty()) {
        tsfTrace("scheduleSharedTrayInputMethodRequest no shared request");
        FCITX_DEBUG()
            << "scheduleSharedTrayInputMethodRequest no shared request"
            << " pid=" << GetCurrentProcessId();
        return false;
    }
    if (!preferredContext) {
        tsfTrace("scheduleSharedTrayInputMethodRequest deferred until key "
                 "event target=" +
                 uniqueName);
        FCITX_DEBUG() << "scheduleSharedTrayInputMethodRequest deferred until "
                         "key event target="
                      << uniqueName << " pid=" << GetCurrentProcessId();
        return false;
    }
    if (uniqueName == deferredSharedTrayInputMethod_) {
        tsfTrace("scheduleSharedTrayInputMethodRequest deferred target=" +
                 uniqueName);
        FCITX_DEBUG() << "scheduleSharedTrayInputMethodRequest deferred target="
                      << uniqueName << " pid=" << GetCurrentProcessId();
        return false;
    }
    const auto current = engine_->currentInputMethod();
    if (current == uniqueName) {
        clearSharedTrayInputMethodRequest();
        persistSharedTrayCurrentInputMethodState(uniqueName);
        tsfTrace("scheduleSharedTrayInputMethodRequest cleared already-current "
                 "target=" +
                 uniqueName);
        FCITX_INFO() << "scheduleSharedTrayInputMethodRequest cleared "
                        "already-current target="
                     << uniqueName << " pid=" << GetCurrentProcessId();
        return false;
    }
    tsfTrace("scheduleSharedTrayInputMethodRequest consume target=" +
             uniqueName + " current=" + current);
    FCITX_INFO()
        << "scheduleSharedTrayInputMethodRequest consume request target="
        << uniqueName << " current=" << current
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
        if (SUCCEEDED(threadMgr_->GetFocus(dm.ReleaseAndGetAddressOf())) &&
            dm) {
            dm->GetTop(ctx.ReleaseAndGetAddressOf());
        }
    }
    if (ctx) {
        ctx->RequestEditSession(clientId_, this, TF_ES_SYNC | TF_ES_READWRITE,
                                &hr);
    }
    if (FAILED(hr)) {
        tsfTrace("scheduleSharedTrayInputMethodRequest RequestEditSession "
                 "failed target=" +
                 uniqueName);
        FCITX_WARN() << "scheduleSharedTrayInputMethodRequest edit session "
                        "failed target="
                     << uniqueName << " hr=0x" << std::hex
                     << static_cast<unsigned long>(hr) << std::dec
                     << " pid=" << GetCurrentProcessId();
        pendingTrayInputMethod_.clear();
        pendingTrayInputMethodFromSharedRequest_ = false;
        return false;
    }
    tsfTrace("scheduleSharedTrayInputMethodRequest RequestEditSession success "
             "target=" +
             uniqueName);
    FCITX_INFO()
        << "scheduleSharedTrayInputMethodRequest edit session scheduled target="
        << uniqueName << " pid=" << GetCurrentProcessId();
    return true;
}

bool Tsf::scheduleSharedTrayStatusActionRequest(ITfContext *preferredContext) {
    if (!engine_ || !pendingTrayStatusAction_.empty() ||
        currentProcessIsExplorer()) {
        return false;
    }
    const auto uniqueName = readSharedTrayStatusActionRequestFile();
    if (uniqueName.empty()) {
        return false;
    }
    pendingTrayStatusAction_ = uniqueName;
    pendingTrayStatusActionFromSharedRequest_ = true;
    HRESULT hr = E_FAIL;
    ComPtr<ITfContext> ctx;
    if (preferredContext) {
        ctx = preferredContext;
    } else {
        ctx = textEditSinkContext_;
    }
    if (!ctx && threadMgr_) {
        ComPtr<ITfDocumentMgr> dm;
        if (SUCCEEDED(threadMgr_->GetFocus(dm.ReleaseAndGetAddressOf())) &&
            dm) {
            dm->GetTop(ctx.ReleaseAndGetAddressOf());
        }
    }
    if (ctx) {
        ctx->RequestEditSession(clientId_, this, TF_ES_SYNC | TF_ES_READWRITE,
                                &hr);
    }
    if (FAILED(hr)) {
        const auto action = pendingTrayStatusAction_;
        pendingTrayStatusAction_.clear();
        pendingTrayStatusActionFromSharedRequest_ = false;
        if (engine_->activateTrayStatusAction(action)) {
            clearSharedTrayStatusActionRequest();
            persistSharedTrayStatusActionState();
            langBarNotifyIconUpdate();
        }
        return false;
    }
    return true;
}

bool Tsf::scheduleSharedTrayPinyinReloadRequest(ITfContext *preferredContext) {
    if (!engine_ || pendingTrayReloadPinyinConfig_ ||
        currentProcessIsExplorer()) {
        return false;
    }
    if (!readSharedTrayPinyinReloadRequestFile()) {
        return false;
    }
    pendingTrayReloadPinyinConfig_ = true;
    HRESULT hr = E_FAIL;
    ComPtr<ITfContext> ctx;
    if (preferredContext) {
        ctx = preferredContext;
    } else {
        ctx = textEditSinkContext_;
    }
    if (!ctx && threadMgr_) {
        ComPtr<ITfDocumentMgr> dm;
        if (SUCCEEDED(threadMgr_->GetFocus(dm.ReleaseAndGetAddressOf())) &&
            dm) {
            dm->GetTop(ctx.ReleaseAndGetAddressOf());
        }
    }
    if (ctx) {
        ctx->RequestEditSession(clientId_, this, TF_ES_SYNC | TF_ES_READWRITE,
                                &hr);
    }
    if (FAILED(hr)) {
        pendingTrayReloadPinyinConfig_ = false;
        if (engine_->reloadPinyinConfig()) {
            clearSharedTrayPinyinReloadRequestFile();
            langBarNotifyIconUpdate();
        }
        return false;
    }
    return true;
}

void Tsf::pushTrayServiceUiEvent() const {
    if (currentProcessIsExplorer()) {
        return;
    }
    if (!ensureStandaloneTrayHelperRunning()) {
        return;
    }
    const HWND helper = standaloneTrayHelperWindow();
    if (!helper) {
        tsfTrace("pushTrayServiceUiEvent missing helper window");
        return;
    }
    fcitx::TrayServiceUiEvent ui = {};
    ui.version = 1;
    ui.engineTrayVisible = TRUE;
    if (!fcitx::sendTrayServiceCopyData(helper, fcitx::kTrayServiceCopyDataUi,
                                        &ui, sizeof(ui))) {
        tsfTrace("pushTrayServiceUiEvent SendMessageTimeout failed");
        return;
    }
    tsfTrace("pushTrayServiceUiEvent sent visible=true");
}

void Tsf::pushTrayServiceStatusEvent() const {
    if (currentProcessIsExplorer()) {
        return;
    }
    if (!ensureStandaloneTrayHelperRunning()) {
        return;
    }
    const HWND helper = standaloneTrayHelperWindow();
    if (!helper) {
        tsfTrace("pushTrayServiceStatusEvent missing helper window");
        return;
    }
    fcitx::TrayServiceStatusEvent status = {};
    status.version = 1;
    status.chineseMode = chineseActive_ ? TRUE : FALSE;
    const auto current =
        engine_ ? engine_->currentInputMethod() : std::string{};
    fcitx::trayServiceCopyUtf8(status.currentInputMethod, current);
    if (engine_) {
        const auto actions = engine_->trayStatusActions();
        status.actionCount = static_cast<UINT>(
            (actions.size() > fcitx::kTrayServiceMaxStatusActionCount)
                ? fcitx::kTrayServiceMaxStatusActionCount
                : actions.size());
        for (UINT i = 0; i < status.actionCount; ++i) {
            fcitx::trayServiceCopyUtf8(status.actions[i].uniqueName,
                                       actions[i].uniqueName);
            fcitx::trayServiceCopyWide(status.actions[i].displayName,
                                       actions[i].displayName);
            status.actions[i].isChecked = actions[i].isChecked ? TRUE : FALSE;
        }
    }
    if (!fcitx::sendTrayServiceCopyData(helper,
                                        fcitx::kTrayServiceCopyDataStatus,
                                        &status, sizeof(status))) {
        tsfTrace("pushTrayServiceStatusEvent SendMessageTimeout failed");
        return;
    }
    tsfTrace("pushTrayServiceStatusEvent sent chinese=" +
             std::string(chineseActive_ ? "true" : "false") +
             " current=" + current);
}

void Tsf::pushTrayServiceStateSnapshot() const {
    pushTrayServiceUiEvent();
    pushTrayServiceStatusEvent();
}

void Tsf::pushTrayServiceTipSessionEvent(bool active) const {
    if (!ensureStandaloneTrayHelperRunning()) {
        return;
    }
    const HWND helper = standaloneTrayHelperWindow();
    if (!helper) {
        tsfTrace("pushTrayServiceTipSessionEvent missing helper window");
        return;
    }
    fcitx::TrayServiceTipSessionEvent event = {};
    event.version = 1;
    event.processId = GetCurrentProcessId();
    event.active = active ? TRUE : FALSE;
    if (!fcitx::sendTrayServiceCopyData(helper,
                                        fcitx::kTrayServiceCopyDataTipSession,
                                        &event, sizeof(event))) {
        tsfTrace("pushTrayServiceTipSessionEvent SendMessageTimeout failed");
        return;
    }
    tsfTrace("pushTrayServiceTipSessionEvent active=" +
             std::string(active ? "true" : "false") + " pid=" +
             std::to_string(static_cast<unsigned long>(event.processId)));
}

void Tsf::langBarNotifyIconUpdate() {
    if (currentProcessIsExplorer()) {
        explorerTrayHelperPrimedOnce();
        return;
    }
    persistSharedTrayChineseModeState(chineseActive_);
    if (engine_) {
        const auto current = engine_->currentInputMethod();
        if (!current.empty()) {
            persistSharedTrayCurrentInputMethodState(current);
        }
    }
    persistSharedTrayStatusActionState();
    pushTrayServiceStateSnapshot();
    updateShellTrayTooltip();
    if (langBarItem_) {
        langBarItem_->notifyModeChanged();
    }
}

void Tsf::traySetChineseModeInEditSession(TfEditCookie ec, bool wantChinese) {
    if (chineseActive_ == wantChinese) {
        clearSharedTrayChineseModeRequest();
        langBarNotifyIconUpdate();
        return;
    }
    chineseActive_ = wantChinese;
    if (!chineseActive_) {
        endCompositionCancel(ec);
    }
    syncCandidateWindow(ec);
    drainCommitsAfterEngine(ec);
    clearSharedTrayChineseModeRequest();
    langBarNotifyIconUpdate();
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
    if (msg == WM_NCDESTROY) {
        const bool hadPinnedDll = self->shellTrayHostDllPinned_;
        KillTimer(hwnd, kShellTrayRetryTimerId);
        self->shellTrayRetryPending_ = false;
        self->shellTrayHostClosing_ = false;
        if (self->shellTrayHostHwnd_ == hwnd) {
            self->shellTrayHostHwnd_ = nullptr;
            self->shellTrayAdded_ = false;
        }
        self->shellTrayHostDllPinned_ = false;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        const LRESULT result = DefWindowProcW(hwnd, msg, wp, lp);
        if (hadPinnedDll) {
            DllRelease();
        }
        return result;
    }
    if (self->shellTrayHostClosing_) {
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
    if (msg == WM_TIMER && wp == kShellTrayRetryTimerId) {
        KillTimer(hwnd, kShellTrayRetryTimerId);
        self->shellTrayRetryPending_ = false;
        tsfTrace("shellTrayHostWndProc retrying Shell_NotifyIcon add");
        self->recreateShellTrayIcon();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool Tsf::initShellTrayIcon() {
    if (currentProcessIsExplorer()) {
        const bool helperReady = explorerTrayHelperPrimedOnce();
        tsfTrace(std::string("initShellTrayIcon explorer helperReady=") +
                 (helperReady ? "true" : "false"));
        return helperReady;
    }
    const bool helperReady = ensureStandaloneTrayHelperRunning();
    tsfTrace(std::string("initShellTrayIcon standalone helperReady=") +
             (helperReady ? "true" : "false"));
    return helperReady;
}

void Tsf::uninitShellTrayIcon() {
    cancelShellTrayRetry();
    shellTrayAdded_ = false;
    if (shellTrayHostHwnd_) {
        tsfTrace("uninitShellTrayIcon destroying shell tray host window");
        shellTrayHostClosing_ = true;
        DestroyWindow(shellTrayHostHwnd_);
    }
    if (shellTrayIconOwned_ && shellTrayIcon_) {
        DestroyIcon(shellTrayIcon_);
    }
    shellTrayIcon_ = nullptr;
    shellTrayIconOwned_ = false;
    shellTrayUseGuidIdentity_ = true;
    shellTrayHostClosing_ = false;
}

void Tsf::scheduleShellTrayRetry(UINT delayMs) {
    if (!currentProcessIsExplorer() || !shellTrayHostHwnd_ ||
        shellTrayHostClosing_) {
        return;
    }
    if (delayMs == 0) {
        delayMs = kShellTrayRetryDelayMs;
    }
    if (!SetTimer(shellTrayHostHwnd_, kShellTrayRetryTimerId, delayMs,
                  nullptr)) {
        tsfTrace("scheduleShellTrayRetry SetTimer failed");
        return;
    }
    shellTrayRetryPending_ = true;
    tsfTrace("scheduleShellTrayRetry armed delayMs=" +
             std::to_string(static_cast<unsigned long>(delayMs)));
}

void Tsf::cancelShellTrayRetry() {
    if (shellTrayHostHwnd_ && shellTrayRetryPending_) {
        KillTimer(shellTrayHostHwnd_, kShellTrayRetryTimerId);
    }
    shellTrayRetryPending_ = false;
}

void Tsf::updateShellTrayTooltip() {
    if (currentProcessIsExplorer()) {
        explorerTrayHelperPrimedOnce();
        return;
    }
    ensureStandaloneTrayHelperRunning();
}

void Tsf::recreateShellTrayIcon() {
    if (currentProcessIsExplorer()) {
        explorerTrayHelperPrimedOnce();
        return;
    }
    ensureStandaloneTrayHelperRunning();
}

void Tsf::showShellTrayContextMenu() {
    if (currentProcessIsExplorer()) {
        tsfTrace("showShellTrayContextMenu explorer helper-only");
        explorerTrayHelperPrimedOnce();
        return;
    }
    POINT pt;
    GetCursorPos(&pt);
    HWND owner =
        shellTrayHostHwnd_ ? shellTrayHostHwnd_ : GetForegroundWindow();
    if (!owner) {
        owner = GetDesktopWindow();
    }
    showShellTrayContextMenuAt(pt, owner);
}

void Tsf::showShellTrayContextMenuAt(POINT pt, HWND owner) {
    if (currentProcessIsExplorer()) {
        tsfTrace(
            std::string("showShellTrayContextMenuAt explorer helper-only pt=") +
            std::to_string(static_cast<long long>(pt.x)) + "," +
            std::to_string(static_cast<long long>(pt.y)) + " owner=" +
            std::to_string(static_cast<unsigned long long>(
                reinterpret_cast<uintptr_t>(owner))));
        explorerTrayHelperPrimedOnce();
        return;
    }
    tsfTrace(std::string("showShellTrayContextMenuAt enter pt=") +
             std::to_string(static_cast<long long>(pt.x)) + "," +
             std::to_string(static_cast<long long>(pt.y)) + " owner=" +
             std::to_string(static_cast<unsigned long long>(
                 reinterpret_cast<uintptr_t>(owner))));
    ScopedDllPin menuPin;
    for (;;) {
        HMENU menu = CreatePopupMenu();
        if (!menu) {
            return;
        }
        auto profileItems = engine_ ? engine_->profileInputMethods()
                                    : std::vector<ProfileInputMethodItem>{};
        if (profileItems.empty()) {
            profileItems = readProfileInputMethodsFromConfig();
        }
        const auto statusActions = engine_
                                       ? engine_->trayStatusActions()
                                       : std::vector<TrayStatusActionItem>{};
        const std::string currentInputMethod =
            engine_ ? engine_->currentInputMethod() : std::string{};
        bool currentIsRime = false;
        bool currentIsShuangpin = currentInputMethod == "shuangpin";
        for (const auto &item : profileItems) {
            if (item.isCurrent && item.uniqueName == "rime") {
                currentIsRime = true;
            }
        }
        tsfTrace("showShellTrayContextMenuAt current=" + currentInputMethod +
                 " currentIsShuangpin=" +
                 std::string(currentIsShuangpin ? "true" : "false"));
        HMENU statusMenu = CreatePopupMenu();
        if (statusMenu) {
            HMENU inputModeMenu = CreatePopupMenu();
            if (inputModeMenu) {
                appendRadioMenuItem(inputModeMenu, IDM_INPUT_MODE_CHINESE,
                                    L"中文模式");
                appendRadioMenuItem(inputModeMenu, IDM_INPUT_MODE_ENGLISH,
                                    L"英文模式");
                CheckMenuRadioItem(inputModeMenu, IDM_INPUT_MODE_CHINESE,
                                   IDM_INPUT_MODE_ENGLISH,
                                   langBarChineseMode()
                                       ? IDM_INPUT_MODE_CHINESE
                                       : IDM_INPUT_MODE_ENGLISH,
                                   MF_BYCOMMAND);
                AppendMenuW(
                    statusMenu, MF_POPUP,
                    reinterpret_cast<UINT_PTR>(inputModeMenu),
                    trayChineseModeMenuText(langBarChineseMode()).c_str());
            }
            if (const auto *item =
                    findTrayStatusAction(statusActions, "fullwidth")) {
                HMENU subMenu = CreatePopupMenu();
                if (subMenu) {
                    appendRadioMenuItem(subMenu, IDM_FULLWIDTH_ON, L"全角");
                    appendRadioMenuItem(subMenu, IDM_FULLWIDTH_OFF, L"半角");
                    CheckMenuRadioItem(
                        subMenu, IDM_FULLWIDTH_ON, IDM_FULLWIDTH_OFF,
                        item->isChecked ? IDM_FULLWIDTH_ON : IDM_FULLWIDTH_OFF,
                        MF_BYCOMMAND);
                    AppendMenuW(statusMenu, MF_POPUP,
                                reinterpret_cast<UINT_PTR>(subMenu),
                                trayStatusToggleMenuText(*item).c_str());
                }
            }
            if (const auto *item =
                    findTrayStatusAction(statusActions, "chttrans")) {
                HMENU subMenu = CreatePopupMenu();
                if (subMenu) {
                    appendRadioMenuItem(subMenu, IDM_SCRIPT_SIMPLIFIED,
                                        L"简体中文");
                    appendRadioMenuItem(subMenu, IDM_SCRIPT_TRADITIONAL,
                                        L"繁体中文");
                    CheckMenuRadioItem(subMenu, IDM_SCRIPT_TRADITIONAL,
                                       IDM_SCRIPT_SIMPLIFIED,
                                       item->isChecked ? IDM_SCRIPT_TRADITIONAL
                                                       : IDM_SCRIPT_SIMPLIFIED,
                                       MF_BYCOMMAND);
                    AppendMenuW(statusMenu, MF_POPUP,
                                reinterpret_cast<UINT_PTR>(subMenu),
                                trayStatusToggleMenuText(*item).c_str());
                }
            }
            if (const auto *item =
                    findTrayStatusAction(statusActions, "punctuation")) {
                HMENU subMenu = CreatePopupMenu();
                if (subMenu) {
                    appendRadioMenuItem(subMenu, IDM_PUNCTUATION_CHINESE,
                                        L"中文标点");
                    appendRadioMenuItem(subMenu, IDM_PUNCTUATION_ENGLISH,
                                        L"英文标点");
                    CheckMenuRadioItem(subMenu, IDM_PUNCTUATION_CHINESE,
                                       IDM_PUNCTUATION_ENGLISH,
                                       item->isChecked
                                           ? IDM_PUNCTUATION_CHINESE
                                           : IDM_PUNCTUATION_ENGLISH,
                                       MF_BYCOMMAND);
                    AppendMenuW(statusMenu, MF_POPUP,
                                reinterpret_cast<UINT_PTR>(subMenu),
                                trayStatusToggleMenuText(*item).c_str());
                }
            }
            if (currentIsShuangpin) {
                const int currentSchemeIndex =
                    shuangpinSchemeIndexFromValue(loadShuangpinSchemeValue());
                HMENU shuangpinMenu = CreatePopupMenu();
                if (shuangpinMenu) {
                    for (size_t i = 0; i < sizeof(kShuangpinSchemes) /
                                               sizeof(kShuangpinSchemes[0]);
                         ++i) {
                        AppendMenuW(
                            shuangpinMenu,
                            MF_STRING |
                                (static_cast<int>(i) == currentSchemeIndex
                                     ? MF_CHECKED
                                     : 0),
                            IDM_SHUANGPIN_SCHEME_BASE + static_cast<UINT>(i),
                            kShuangpinSchemes[i].menuText);
                    }
                    AppendMenuW(statusMenu, MF_POPUP,
                                reinterpret_cast<UINT_PTR>(shuangpinMenu),
                                L"双拼方案");
                }
            }
            AppendMenuW(statusMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(statusMenu, MF_STRING, IDM_SETTINGS_GUI,
                        L"Fcitx5 设置...");
            AppendMenuW(statusMenu, MF_STRING, IDM_OPEN_CONFIG_DIR,
                        L"打开配置文件夹");
            AppendMenuW(statusMenu, MF_STRING, IDM_OPEN_LOG_DIR,
                        L"打开 Fcitx5 日志目录");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(statusMenu),
                        L"输入状态");
        }
        HMENU imMenu = CreatePopupMenu();
        if (imMenu) {
            if (profileItems.empty()) {
                AppendMenuW(imMenu, MF_STRING | MF_GRAYED,
                            IDM_INPUT_METHOD_BASE,
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
            AppendMenuW(menu, MF_STRING, IDM_RIME_OPEN_LOG_DIR,
                        L"打开中州韵日志目录");
        }
        if (owner && owner != GetDesktopWindow()) {
            SetForegroundWindow(owner);
        }
        const UINT cmd = TrackPopupMenu(
            menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
            pt.x, pt.y, 0, owner ? owner : GetDesktopWindow(), nullptr);
        if (owner && owner != GetDesktopWindow()) {
            PostMessageW(owner, WM_NULL, 0, 0);
        }
        DestroyMenu(menu);

        bool reopenMenu = false;
        switch (cmd) {
        case 0:
            return;
        case IDM_INPUT_MODE_CHINESE:
            if (!langBarChineseMode()) {
                langBarScheduleSetChineseMode(true);
            }
            reopenMenu = true;
            break;
        case IDM_INPUT_MODE_ENGLISH:
            if (langBarChineseMode()) {
                langBarScheduleSetChineseMode(false);
            }
            reopenMenu = true;
            break;
        case IDM_SETTINGS_GUI:
            launchSettingsGui();
            return;
        case IDM_OPEN_CONFIG_DIR:
            exploreUserFcitxConfig();
            return;
        case IDM_RIME_DEPLOY:
            if (!launchRimeDeployWithNotification(shellTrayHostHwnd_) &&
                engine_) {
                engine_->invokeInputMethodSubConfig("rime", "deploy");
            }
            return;
        case IDM_RIME_SYNC:
            if (engine_) {
                engine_->invokeInputMethodSubConfig("rime", "sync");
            }
            return;
        case IDM_RIME_OPEN_USER_DIR:
            exploreUserRimeConfig();
            return;
        case IDM_RIME_OPEN_LOG_DIR:
            exploreRimeLogDir();
            return;
        case IDM_OPEN_LOG_DIR:
            exploreFcitx5LogDir();
            return;
        default:
            if (cmd == IDM_FULLWIDTH_ON || cmd == IDM_FULLWIDTH_OFF ||
                cmd == IDM_SCRIPT_SIMPLIFIED || cmd == IDM_SCRIPT_TRADITIONAL ||
                cmd == IDM_PUNCTUATION_CHINESE ||
                cmd == IDM_PUNCTUATION_ENGLISH) {
                if (engine_) {
                    const char *actionName = nullptr;
                    bool targetChecked = false;
                    if (cmd == IDM_FULLWIDTH_ON || cmd == IDM_FULLWIDTH_OFF) {
                        actionName = "fullwidth";
                        targetChecked = cmd == IDM_FULLWIDTH_ON;
                    } else if (cmd == IDM_SCRIPT_SIMPLIFIED ||
                               cmd == IDM_SCRIPT_TRADITIONAL) {
                        actionName = "chttrans";
                        targetChecked = cmd == IDM_SCRIPT_TRADITIONAL;
                    } else {
                        actionName = "punctuation";
                        targetChecked = cmd == IDM_PUNCTUATION_CHINESE;
                    }
                    if (const auto *item =
                            findTrayStatusAction(statusActions, actionName);
                        item && item->isChecked != targetChecked) {
                        engine_->activateTrayStatusAction(item->uniqueName);
                        langBarNotifyIconUpdate();
                    }
                }
                reopenMenu = true;
                break;
            }
            if (cmd >= IDM_INPUT_METHOD_BASE &&
                cmd < IDM_INPUT_METHOD_BASE + profileItems.size()) {
                langBarScheduleActivateInputMethod(
                    profileItems[cmd - IDM_INPUT_METHOD_BASE].uniqueName);
                return;
            }
            if (cmd >= IDM_SHUANGPIN_SCHEME_BASE &&
                cmd < IDM_SHUANGPIN_SCHEME_BASE +
                          (sizeof(kShuangpinSchemes) /
                           sizeof(kShuangpinSchemes[0]))) {
                const auto index = cmd - IDM_SHUANGPIN_SCHEME_BASE;
                const bool ok =
                    saveShuangpinSchemeValue(kShuangpinSchemes[index].value);
                if (ok && engine_) {
                    engine_->reloadPinyinConfig();
                    langBarNotifyIconUpdate();
                }
                tsfTrace(std::string(ok ? "saved" : "failed") +
                         " shuangpin scheme=" + kShuangpinSchemes[index].value);
                return;
            }
            break;
        }
        if (!reopenMenu) {
            return;
        }
        GetCursorPos(&pt);
    }
}

} // namespace fcitx
