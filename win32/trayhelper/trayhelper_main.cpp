#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

namespace {

struct ProfileInputMethodItem {
    std::string uniqueName;
    std::wstring displayName;
    bool isCurrent = false;
};

constexpr UINT kShellTrayCallback = WM_APP + 88;
constexpr UINT_PTR kRefreshTimerId = 1;
constexpr UINT_PTR kRetryTimerId = 2;
constexpr UINT kRefreshIntervalMs = 1000;
constexpr UINT kRetryDelayMs = 1500;
constexpr UINT kShellTrayUid = 1;
constexpr wchar_t kWindowClassName[] = L"Fcitx5StandaloneTrayHelperWindow";
constexpr wchar_t kMutexName[] = L"Local\\Fcitx5StandaloneTrayHelperMutex";

const GUID kFcitxShellTrayNotifyGuid = {
    0x8b4d3a2f, 0x1e0c, 0x4a5b, {0x9c, 0x8d, 0x7e, 0x6f, 0x5a, 0x4b, 0x3c, 0x2e}};

enum TrayMenuId : UINT {
    IDM_CHINESE = 0x4100,
    IDM_ENGLISH,
    IDM_SETTINGS_GUI,
    IDM_OPEN_CONFIG_DIR,
    IDM_OPEN_LOG_DIR,
    IDM_RIME_DEPLOY,
    IDM_RIME_OPEN_USER_DIR,
    IDM_RIME_OPEN_LOG_DIR,
    IDM_INPUT_METHOD_BASE = 0x4300,
};

HWND g_hwnd = nullptr;
HICON g_icon = nullptr;
bool g_iconOwned = false;
bool g_trayAdded = false;
bool g_useGuidIdentity = true;
bool g_retryPending = false;
UINT g_taskbarCreatedMessage = 0;
HANDLE g_singleInstanceMutex = nullptr;
bool g_lastChineseMode = true;
std::string g_lastCurrentIm;

struct RimeDeployMonitorData {
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    HWND trayHwnd = nullptr;
    HANDLE waitHandle = nullptr;
};

std::unordered_map<HANDLE, std::unique_ptr<RimeDeployMonitorData>>
    g_rimeDeployMonitors;

std::filesystem::path helperExePath() {
    WCHAR exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        return {};
    }
    return exePath;
}

std::filesystem::path portableRoot() {
    const auto exe = helperExePath();
    if (exe.empty()) {
        return {};
    }
    return exe.parent_path().parent_path();
}

std::filesystem::path appDataRoot() {
    WCHAR appData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return {};
    }
    return std::filesystem::path(appData) / L"Fcitx5";
}

std::filesystem::path traceLogPath() { return appDataRoot() / L"tsf-trace.log"; }

void trayHelperTrace(const std::string &message) {
    const auto path = traceLogPath();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out.is_open()) {
        return;
    }
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    out << st.wYear << "-";
    if (st.wMonth < 10) {
        out << '0';
    }
    out << st.wMonth << "-";
    if (st.wDay < 10) {
        out << '0';
    }
    out << st.wDay << " ";
    if (st.wHour < 10) {
        out << '0';
    }
    out << st.wHour << ":";
    if (st.wMinute < 10) {
        out << '0';
    }
    out << st.wMinute << ":";
    if (st.wSecond < 10) {
        out << '0';
    }
    out << st.wSecond << " [pid=" << GetCurrentProcessId() << "] helper "
        << message << "\n";
}

std::filesystem::path sharedTrayInputMethodRequestFile() {
    return appDataRoot() / L"pending-tray-input-method.txt";
}

std::filesystem::path sharedTrayCurrentInputMethodFile() {
    return appDataRoot() / L"current-tray-input-method.txt";
}

std::filesystem::path sharedTrayChineseModeRequestFile() {
    return appDataRoot() / L"pending-tray-chinese-mode.txt";
}

std::filesystem::path sharedTrayChineseModeStateFile() {
    return appDataRoot() / L"current-tray-chinese-mode.txt";
}

std::string trimSharedTrayValue(std::string value) {
    while (!value.empty() &&
           (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' ||
            value.back() == '\t')) {
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

std::string readSharedTrayCurrentInputMethodState() {
    auto current = readSharedTrayTextFile(sharedTrayCurrentInputMethodFile());
    if (current.empty()) {
        current = readSharedTrayTextFile(sharedTrayInputMethodRequestFile());
    }
    return current;
}

bool readSharedTrayChineseModeState(bool *value) {
    return readSharedTrayChineseModeFile(sharedTrayChineseModeStateFile(), value);
}

void persistSharedTrayChineseModeState(bool chineseMode) {
    writeSharedTrayChineseModeFile(sharedTrayChineseModeStateFile(), chineseMode);
}

void persistSharedTrayChineseModeRequest(bool chineseMode) {
    writeSharedTrayChineseModeFile(sharedTrayChineseModeRequestFile(), chineseMode);
}

void persistSharedTrayCurrentInputMethodState(const std::string &uniqueName) {
    if (!uniqueName.empty()) {
        writeSharedTrayTextFile(sharedTrayCurrentInputMethodFile(), uniqueName);
    }
}

void persistSharedTrayInputMethodRequest(const std::string &uniqueName) {
    if (!uniqueName.empty()) {
        writeSharedTrayTextFile(sharedTrayInputMethodRequestFile(), uniqueName);
    }
}

std::filesystem::path sharedTrayProfileFile() {
    const auto roaming = appDataRoot() / L"config" / L"fcitx5" / L"profile";
    const auto portable = portableRoot() / L"config" / L"fcitx5" / L"profile";
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
    std::ifstream in(profilePath, std::ios::binary);
    if (!in.is_open()) {
        trayHelperTrace("readProfileInputMethodsFromConfig failed path=" +
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
    trayHelperTrace("readProfileInputMethodsFromConfig path=" + profilePath.string() +
                    " items=" +
                    std::to_string(static_cast<unsigned long>(items.size())) +
                    " current=" + current);
    return items;
}

std::wstring trayInputMethodMenuText(const ProfileInputMethodItem &item) {
    if (item.uniqueName == "pinyin") {
        return L"\x62fc\x97f3";
    }
    if (item.uniqueName == "wbx") {
        return L"\x4e94\x7b14\x5b57\x578b";
    }
    if (item.uniqueName == "wubi98") {
        return L"\x4e94\x7b14" L"98";
    }
    if (item.uniqueName == "chewing") {
        return L"\x65b0\x9177\x97f3";
    }
    if (item.uniqueName == "rime") {
        return L"\x4e2d\x5dde\x97f5";
    }
    if (!item.displayName.empty()) {
        return item.displayName;
    }
    return std::wstring(item.uniqueName.begin(), item.uniqueName.end());
}

void openDirectoryInDetachedExplorer(const std::wstring &dir) {
    if (dir.empty()) {
        return;
    }
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

bool launchDetachedProcess(const std::wstring &application,
                           const std::wstring &arguments,
                           const std::wstring &workingDirectory = {}) {
    if (application.empty()) {
        return false;
    }
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

void launchSettingsGui() {
    const auto exe = portableRoot() / L"bin" / L"fcitx5-config-win32.exe";
    const auto exeStr = exe.wstring();
    launchDetachedProcess(exeStr, L"", exe.parent_path().wstring());
}

void exploreUserFcitxConfig() {
    openDirectoryInDetachedExplorer(appDataRoot().wstring());
}

void exploreUserRimeConfig() {
    const auto dir = appDataRoot() / L"rime";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    openDirectoryInDetachedExplorer(dir.wstring());
}

void exploreFcitx5LogDir() {
    const auto dir = appDataRoot() / L"log";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    openDirectoryInDetachedExplorer(dir.wstring());
}

void exploreRimeLogDir() {
    const auto dir = appDataRoot() / L"rime";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    openDirectoryInDetachedExplorer(dir.wstring());
}

std::filesystem::path fcitx5RimeUserDir() {
    const wchar_t *envDir = _wgetenv(L"FCITX_RIME_USER_DIR");
    if (envDir && *envDir) {
        return envDir;
    }
    return appDataRoot() / L"rime";
}

std::filesystem::path locateRimeDeployer() {
    const auto root = portableRoot();
    const std::filesystem::path candidates[] = {
        root / L"bin" / L"rime_deployer.exe",
        root / L"rime_deployer.exe",
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

void fillShellTrayNidIdentity(NOTIFYICONDATAW *nid, bool useGuid = true) {
    ZeroMemory(nid, sizeof(*nid));
    nid->cbSize = sizeof(*nid);
    nid->hWnd = g_hwnd;
    nid->uID = kShellTrayUid;
    if (useGuid) {
        nid->uFlags |= NIF_GUID;
        nid->guidItem = kFcitxShellTrayNotifyGuid;
    }
}

void populateTrayNid(NOTIFYICONDATAW *nid, bool useGuid, bool chineseMode) {
    fillShellTrayNidIdentity(nid, useGuid);
    nid->uFlags |= NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid->uCallbackMessage = kShellTrayCallback;
    nid->hIcon = g_icon;
    if (chineseMode) {
        wcsncpy_s(nid->szTip,
                  L"Fcitx5 \x2014 \x4e2d\x6587\nShift / Ctrl+Space \x5207\x6362\x4e2d/\x82f1",
                  _TRUNCATE);
    } else {
        wcsncpy_s(nid->szTip,
                  L"Fcitx5 \x2014 English\nShift / Ctrl+Space \x5207\x6362\x4e2d/\x82f1",
                  _TRUNCATE);
    }
}

void showTrayBalloon(const wchar_t *title, const wchar_t *text,
                     DWORD infoFlags = NIIF_INFO) {
    if (!g_hwnd || !g_trayAdded) {
        return;
    }
    NOTIFYICONDATAW nid = {};
    fillShellTrayNidIdentity(&nid, g_useGuidIdentity);
    nid.uFlags |= NIF_INFO;
    nid.dwInfoFlags = infoFlags;
    wcsncpy_s(nid.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(nid.szInfo, text, _TRUNCATE);
    nid.uTimeout = 3000;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

bool addTrayIcon(bool chineseMode) {
    if (!g_hwnd || !g_icon) {
        return false;
    }
    NOTIFYICONDATAW nid = {};
    populateTrayNid(&nid, true, chineseMode);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (Shell_NotifyIconW(NIM_ADD, &nid)) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        g_useGuidIdentity = true;
        g_trayAdded = true;
        g_retryPending = false;
        KillTimer(g_hwnd, kRetryTimerId);
        trayHelperTrace("addTrayIcon success useGuid=true");
        return true;
    }
    const DWORD guidError = GetLastError();

    NOTIFYICONDATAW legacy = {};
    populateTrayNid(&legacy, false, chineseMode);
    Shell_NotifyIconW(NIM_DELETE, &legacy);
    if (Shell_NotifyIconW(NIM_ADD, &legacy)) {
        legacy.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &legacy);
        g_useGuidIdentity = false;
        g_trayAdded = true;
        g_retryPending = false;
        KillTimer(g_hwnd, kRetryTimerId);
        trayHelperTrace("addTrayIcon success useGuid=false guid_gle=" +
                        std::to_string(static_cast<unsigned long>(guidError)));
        return true;
    }
    const DWORD legacyError = GetLastError();
    trayHelperTrace("addTrayIcon failed guid_gle=" +
                    std::to_string(static_cast<unsigned long>(guidError)) +
                    " legacy_gle=" +
                    std::to_string(static_cast<unsigned long>(legacyError)));
    g_trayAdded = false;
    if (!g_retryPending) {
        SetTimer(g_hwnd, kRetryTimerId, kRetryDelayMs, nullptr);
        g_retryPending = true;
    }
    return false;
}

void removeTrayIcon() {
    if (!g_trayAdded || !g_hwnd) {
        return;
    }
    NOTIFYICONDATAW nid = {};
    fillShellTrayNidIdentity(&nid, g_useGuidIdentity);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = false;
}

void updateTrayTooltip(bool chineseMode) {
    if (!g_trayAdded || !g_hwnd) {
        return;
    }
    NOTIFYICONDATAW nid = {};
    fillShellTrayNidIdentity(&nid, g_useGuidIdentity);
    nid.uFlags |= NIF_TIP | NIF_SHOWTIP;
    if (chineseMode) {
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

void refreshTrayState() {
    bool chineseMode = true;
    readSharedTrayChineseModeState(&chineseMode);
    const std::string current = readSharedTrayCurrentInputMethodState();
    if (!g_trayAdded) {
        addTrayIcon(chineseMode);
    } else if (chineseMode != g_lastChineseMode || current != g_lastCurrentIm) {
        updateTrayTooltip(chineseMode);
    }
    g_lastChineseMode = chineseMode;
    g_lastCurrentIm = current;
}

HICON loadPenguinIconNearExe(unsigned cx, unsigned cy) {
    const auto iconPath = helperExePath().parent_path() / L"penguin.ico";
    HICON icon = reinterpret_cast<HICON>(LoadImageW(
        nullptr, iconPath.c_str(), IMAGE_ICON, static_cast<int>(cx),
        static_cast<int>(cy), LR_LOADFROMFILE));
    if (!icon) {
        icon = reinterpret_cast<HICON>(LoadImageW(
            nullptr, MAKEINTRESOURCEW(32512), IMAGE_ICON, static_cast<int>(cx),
            static_cast<int>(cy), LR_SHARED));
    }
    return icon;
}

static void CALLBACK rimeDeployWaitCallback(PVOID lpParameter,
                                            BOOLEAN /*timerOrWaitFired*/) {
    auto it = g_rimeDeployMonitors.find(lpParameter);
    if (it == g_rimeDeployMonitors.end() || !it->second) {
        return;
    }
    auto &data = it->second;
    DWORD exitCode = 0;
    BOOL gotExitCode = GetExitCodeProcess(data->process, &exitCode);
    if (data->waitHandle) {
        UnregisterWait(data->waitHandle);
    }
    CloseHandle(data->thread);
    CloseHandle(data->process);
    if (data->trayHwnd) {
        if (gotExitCode && exitCode == 0) {
            showTrayBalloon(L"\x4e2d\x5dde\x97f5\x90e8\x7f72",
                            L"\x90e8\x7f72\x6210\x529f\x5b8c\x6210\xff01",
                            NIIF_INFO);
        } else {
            const std::wstring msg =
                L"\x90e8\x7f72\x5931\x8d25 (\x9000\x51fa\x7801: " +
                std::to_wstring(exitCode) + L")";
            showTrayBalloon(L"\x4e2d\x5dde\x97f5\x90e8\x7f72", msg.c_str(),
                            NIIF_ERROR);
        }
    }
    g_rimeDeployMonitors.erase(it);
}

bool launchRimeDeployWithNotification() {
    const auto deployer = locateRimeDeployer();
    const auto root = portableRoot();
    const auto userDir = fcitx5RimeUserDir();
    if (deployer.empty() || root.empty() || userDir.empty()) {
        showTrayBalloon(L"\x4e2d\x5dde\x97f5\x90e8\x7f72",
                        L"\x9519\x8bef\xff1a\x65e0\x6cd5\x542f\x52a8 rime_deployer",
                        NIIF_ERROR);
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(userDir, ec);
    const auto sharedDir = root / L"share" / L"rime-data";
    const auto stagingDir = userDir / L"build";
    std::filesystem::create_directories(stagingDir, ec);
    const std::wstring args = L"--build \"" + userDir.wstring() + L"\" \"" +
                              sharedDir.wstring() + L"\" \"" +
                              stagingDir.wstring() + L"\"";
    std::wstring commandLine = L"\"" + deployer.wstring() + L"\" " + args;
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    const auto cwdString = deployer.parent_path().wstring();
    const wchar_t *cwd = cwdString.empty() ? nullptr : cwdString.c_str();
    if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, cwd, &si, &pi)) {
        showTrayBalloon(L"\x4e2d\x5dde\x97f5\x90e8\x7f72",
                        L"\x9519\x8bef\xff1a\x542f\x52a8\x90e8\x7f72\x5de5\x5177\x5931\x8d25",
                        NIIF_ERROR);
        return false;
    }
    showTrayBalloon(L"\x4e2d\x5dde\x97f5\x90e8\x7f72",
                    L"\x6b63\x5728\x90e8\x7f72\x4e2d\x5dde\x97f5...", NIIF_INFO);
    auto monitor = std::make_unique<RimeDeployMonitorData>();
    monitor->process = pi.hProcess;
    monitor->thread = pi.hThread;
    monitor->trayHwnd = g_hwnd;
    HANDLE key = pi.hProcess;
    g_rimeDeployMonitors[key] = std::move(monitor);
    if (!RegisterWaitForSingleObject(&g_rimeDeployMonitors[key]->waitHandle,
                                     pi.hProcess, rimeDeployWaitCallback, key,
                                     INFINITE, WT_EXECUTEONLYONCE)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        g_rimeDeployMonitors.erase(key);
        return false;
    }
    return true;
}

void showContextMenu() {
    auto profileItems = readProfileInputMethodsFromConfig();
    bool chineseMode = true;
    readSharedTrayChineseModeState(&chineseMode);
    bool currentIsRime = false;
    for (const auto &item : profileItems) {
        if (item.isCurrent && item.uniqueName == "rime") {
            currentIsRime = true;
            break;
        }
    }

    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    AppendMenuW(menu, MF_STRING | (chineseMode ? MF_CHECKED : 0), IDM_CHINESE,
                L"\x4e2d\x6587\x6a21\x5f0f");
    AppendMenuW(menu, MF_STRING | (!chineseMode ? MF_CHECKED : 0), IDM_ENGLISH,
                L"\x82f1\x6587\x6a21\x5f0f\xff08\x76f4\x63a5\x952e\x5165\xff09");

    HMENU imMenu = CreatePopupMenu();
    if (imMenu) {
        if (profileItems.empty()) {
            AppendMenuW(imMenu, MF_STRING | MF_GRAYED, IDM_INPUT_METHOD_BASE,
                        L"\x5f53\x524d profile \x4e2d\x6ca1\x6709\x53ef\x5207\x6362\x8f93\x5165\x6cd5");
        } else {
            for (size_t i = 0; i < profileItems.size(); ++i) {
                AppendMenuW(imMenu,
                            MF_STRING | (profileItems[i].isCurrent ? MF_CHECKED : 0),
                            IDM_INPUT_METHOD_BASE + static_cast<UINT>(i),
                            trayInputMethodMenuText(profileItems[i]).c_str());
            }
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(imMenu),
                    L"\x5207\x6362\x8f93\x5165\x6cd5");
    }

    if (currentIsRime) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_RIME_DEPLOY,
                    L"\x91cd\x65b0\x90e8\x7f72\x4e2d\x5dde\x97f5");
        AppendMenuW(menu, MF_STRING, IDM_RIME_OPEN_USER_DIR,
                    L"\x6253\x5f00\x4e2d\x5dde\x97f5\x914d\x7f6e\x76ee\x5f55");
        AppendMenuW(menu, MF_STRING, IDM_RIME_OPEN_LOG_DIR,
                    L"\x6253\x5f00\x4e2d\x5dde\x97f5\x65e5\x5fd7\x76ee\x5f55");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS_GUI,
                L"\x6253\x5f00\x8bbe\x7f6e\x754c\x9762...");
    AppendMenuW(menu, MF_STRING, IDM_OPEN_CONFIG_DIR,
                L"\x6253\x5f00\x914d\x7f6e\x6587\x4ef6\x5939");
    AppendMenuW(menu, MF_STRING, IDM_OPEN_LOG_DIR,
                L"\x6253\x5f00\x65e5\x5fd7\x6587\x4ef6\x5939");

    POINT pt = {};
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    const UINT cmd =
        TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD |
                                  TPM_NONOTIFY,
                       pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (cmd) {
    case IDM_CHINESE:
        persistSharedTrayChineseModeRequest(true);
        persistSharedTrayChineseModeState(true);
        refreshTrayState();
        break;
    case IDM_ENGLISH:
        persistSharedTrayChineseModeRequest(false);
        persistSharedTrayChineseModeState(false);
        refreshTrayState();
        break;
    case IDM_SETTINGS_GUI:
        launchSettingsGui();
        break;
    case IDM_OPEN_CONFIG_DIR:
        exploreUserFcitxConfig();
        break;
    case IDM_OPEN_LOG_DIR:
        exploreFcitx5LogDir();
        break;
    case IDM_RIME_DEPLOY:
        launchRimeDeployWithNotification();
        break;
    case IDM_RIME_OPEN_USER_DIR:
        exploreUserRimeConfig();
        break;
    case IDM_RIME_OPEN_LOG_DIR:
        exploreRimeLogDir();
        break;
    default:
        if (cmd >= IDM_INPUT_METHOD_BASE &&
            cmd < IDM_INPUT_METHOD_BASE + profileItems.size()) {
            const auto &item = profileItems[cmd - IDM_INPUT_METHOD_BASE];
            persistSharedTrayInputMethodRequest(item.uniqueName);
            persistSharedTrayCurrentInputMethodState(item.uniqueName);
            persistSharedTrayChineseModeRequest(true);
            persistSharedTrayChineseModeState(true);
            refreshTrayState();
        }
        break;
    }
}

LRESULT CALLBACK trayHelperWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kShellTrayCallback) {
        const UINT code = LOWORD(static_cast<DWORD_PTR>(lp));
        if (code == WM_LBUTTONUP || code == NIN_SELECT || code == NIN_KEYSELECT) {
            bool chineseMode = true;
            readSharedTrayChineseModeState(&chineseMode);
            persistSharedTrayChineseModeRequest(!chineseMode);
            persistSharedTrayChineseModeState(!chineseMode);
            refreshTrayState();
        } else if (code == WM_RBUTTONUP || code == WM_CONTEXTMENU) {
            showContextMenu();
        }
        return 0;
    }
    if (g_taskbarCreatedMessage != 0 && msg == g_taskbarCreatedMessage) {
        g_trayAdded = false;
        refreshTrayState();
        return 0;
    }
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, kRefreshTimerId, kRefreshIntervalMs, nullptr);
        refreshTrayState();
        return 0;
    case WM_TIMER:
        if (wp == kRefreshTimerId) {
            refreshTrayState();
            return 0;
        }
        if (wp == kRetryTimerId) {
            g_retryPending = false;
            KillTimer(hwnd, kRetryTimerId);
            refreshTrayState();
            return 0;
        }
        break;
    case WM_DESTROY:
        KillTimer(hwnd, kRefreshTimerId);
        KillTimer(hwnd, kRetryTimerId);
        removeTrayIcon();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!g_singleInstanceMutex) {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_singleInstanceMutex);
        return 0;
    }

    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);
    g_icon = loadPenguinIconNearExe(static_cast<unsigned>(cx),
                                    static_cast<unsigned>(cy));
    std::error_code ec;
    g_iconOwned =
        g_icon != nullptr &&
        std::filesystem::exists(helperExePath().parent_path() / L"penguin.ico", ec);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = trayHelperWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    const ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (g_iconOwned && g_icon) {
            DestroyIcon(g_icon);
        }
        CloseHandle(g_singleInstanceMutex);
        return 1;
    }

    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClassName, L"", WS_POPUP,
                             0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!g_hwnd) {
        if (g_iconOwned && g_icon) {
            DestroyIcon(g_icon);
        }
        CloseHandle(g_singleInstanceMutex);
        return 1;
    }

    trayHelperTrace("startup complete");

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    trayHelperTrace("shutdown");
    if (g_iconOwned && g_icon) {
        DestroyIcon(g_icon);
    }
    if (g_singleInstanceMutex) {
        ReleaseMutex(g_singleInstanceMutex);
        CloseHandle(g_singleInstanceMutex);
    }
    return 0;
}
