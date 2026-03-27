#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include "../tsf/TrayServiceIpc.h"

#include <cwchar>
#include <filesystem>
#include <fstream>
#include <sstream>
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

struct TrayStatusActionItem {
    std::string uniqueName;
    std::wstring displayName;
    bool isChecked = false;
};

constexpr UINT kShellTrayCallback = WM_APP + 88;
constexpr UINT kReopenContextMenuMessage = WM_APP + 89;
constexpr UINT_PTR kRetryTimerId = 1;
constexpr UINT kRetryDelayMs = 1500;
/// Coalesce ui/status + focus IPC into one Shell_NotifyIcon pass (reduces
/// remove/add jitter during TSF ActivateEx bursts; same idea as debouncing UI).
constexpr UINT_PTR kTrayRefreshDebounceTimerId = 2;
constexpr UINT kTrayRefreshDebounceMs = 85;
/// Reconcile stale tip-session PIDs if a Deactivate IPC was dropped.
constexpr UINT_PTR kPeriodicTipSessionTimerId = 3;
constexpr UINT kPeriodicTipSessionMs = 45000;
constexpr UINT kShellTrayUid = 1;
constexpr wchar_t kMutexName[] = L"Local\\Fcitx5StandaloneTrayHelperMutex";

const GUID kFcitxShellTrayNotifyGuid = {
    0x8b4d3a2f, 0x1e0c, 0x4a5b, {0x9c, 0x8d, 0x7e, 0x6f, 0x5a, 0x4b, 0x3c, 0x2e}};

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
    IDM_RIME_OPEN_USER_DIR,
    IDM_RIME_OPEN_LOG_DIR,
    IDM_STATUS_ACTION_BASE = 0x4200,
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
POINT g_reopenMenuPoint = {};
bool g_reopenMenuPending = false;
bool g_contextMenuShowing = false;

struct TrayServiceState {
    /// TSF **Ui** message received (engine tray visibility).
    bool hasUi = false;
    /// TSF **Status** message received (mode / IM / actions).
    bool hasStatus = false;
    /// From TSF **Ui** only (not derived from per-host document focus).
    bool visible = true;
    bool chineseMode = true;
    std::string currentInputMethod;
    std::vector<TrayStatusActionItem> statusActions;
};

TrayServiceState g_trayState;
/// Per-PID ref count for ITfTextInputProcessor ActivateEx/Deactivate pairs.
std::unordered_map<DWORD, int> g_tipSessionRefCount;

/// Helper-owned foreground hint: merged with tip-session PIDs for diagnostics and
/// future policy (visibility is still driven by tip sessions + engine **Ui**).
struct ForegroundTrayValidityState {
    DWORD foregroundPid = 0;
    bool foregroundPidIsTipSession = false;
    bool foregroundIsShellOrHelper = false;
};

ForegroundTrayValidityState g_foregroundTrayValidity;

bool queryProcessImageBaseName(DWORD processId, std::wstring *outBase) {
    if (!outBase) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                 processId);
    if (!process) {
        return false;
    }
    wchar_t path[4096];
    DWORD size = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!ok) {
        return false;
    }
    const wchar_t *slash = wcsrchr(path, L'\\');
    *outBase = slash ? std::wstring(slash + 1) : std::wstring(path);
    return true;
}

void updateForegroundTrayValidityState() {
    HWND fg = GetForegroundWindow();
    if (!fg) {
        g_foregroundTrayValidity.foregroundPid = 0;
        g_foregroundTrayValidity.foregroundPidIsTipSession = false;
        g_foregroundTrayValidity.foregroundIsShellOrHelper = false;
        return;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    g_foregroundTrayValidity.foregroundPid = pid;
    const auto it = g_tipSessionRefCount.find(pid);
    g_foregroundTrayValidity.foregroundPidIsTipSession =
        (it != g_tipSessionRefCount.end() && it->second > 0);
    if (pid == GetCurrentProcessId()) {
        g_foregroundTrayValidity.foregroundIsShellOrHelper = true;
        return;
    }
    std::wstring base;
    g_foregroundTrayValidity.foregroundIsShellOrHelper =
        queryProcessImageBaseName(pid, &base) &&
        (_wcsicmp(base.c_str(), L"explorer.exe") == 0);
}

struct RimeDeployMonitorData {
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    HWND trayHwnd = nullptr;
    HANDLE waitHandle = nullptr;
};

std::unordered_map<HANDLE, std::unique_ptr<RimeDeployMonitorData>>
    g_rimeDeployMonitors;

std::wstring utf8ToWide(const std::string &utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                      static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        wide.data(), n);
    return wide;
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

std::filesystem::path helperExePath() {
    WCHAR exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        return {};
    }
    return exePath;
}

std::string currentProcessExeBaseNameUtf8() {
    const auto baseName = helperExePath().filename().wstring();
    return std::string(baseName.begin(), baseName.end());
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
    out << st.wSecond << " [pid=" << GetCurrentProcessId()
        << " process=" << currentProcessExeBaseNameUtf8() << "] helper "
        << message << "\n";
}

LONG WINAPI trayHelperUnhandledExceptionFilter(EXCEPTION_POINTERS *ep) {
    std::ostringstream ss;
    ss << "unhandled exception code=0x" << std::hex
       << static_cast<unsigned long>(
              ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0)
       << " address=0x"
       << reinterpret_cast<std::uintptr_t>(
              ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress
                                        : nullptr);
    trayHelperTrace(ss.str());
    return EXCEPTION_CONTINUE_SEARCH;
}

void installTrayHelperCrashLogging() {
    SetUnhandledExceptionFilter(trayHelperUnhandledExceptionFilter);
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

std::filesystem::path sharedTrayStatusActionRequestFile() {
    return appDataRoot() / L"pending-tray-status-action.txt";
}

std::filesystem::path sharedTrayStatusActionStateFile() {
    return appDataRoot() / L"current-tray-status-actions.txt";
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

void persistSharedTrayStatusActionRequest(const std::string &uniqueName) {
    if (!uniqueName.empty()) {
        writeSharedTrayTextFile(sharedTrayStatusActionRequestFile(), uniqueName);
    }
}

std::vector<TrayStatusActionItem> readSharedTrayStatusActions() {
    std::vector<TrayStatusActionItem> items;
    std::ifstream in(sharedTrayStatusActionStateFile(), std::ios::binary);
    if (!in.is_open()) {
        return items;
    }
    std::string line;
    while (std::getline(in, line)) {
        line = trimSharedTrayValue(std::move(line));
        if (line.empty()) {
            continue;
        }
        const size_t firstTab = line.find('\t');
        const size_t secondTab =
            firstTab == std::string::npos ? std::string::npos : line.find('\t', firstTab + 1);
        if (firstTab == std::string::npos || secondTab == std::string::npos) {
            continue;
        }
        const std::string uniqueName = line.substr(0, firstTab);
        const std::string checked = line.substr(firstTab + 1, secondTab - firstTab - 1);
        const std::string label = line.substr(secondTab + 1);
        if (uniqueName.empty() || label.empty()) {
            continue;
        }
        items.push_back(TrayStatusActionItem{
            uniqueName, utf8ToWide(label), checked == "1" || checked == "true"});
    }
    return items;
}

bool isProcessStillRunning(DWORD processId) {
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE, processId);
    if (!process) {
        return false;
    }
    const DWORD waitResult = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return waitResult == WAIT_TIMEOUT;
}

void pruneInactiveTipSessions() {
    for (auto it = g_tipSessionRefCount.begin();
         it != g_tipSessionRefCount.end();) {
        if (it->second <= 0 || !isProcessStillRunning(it->first)) {
            if (it->second > 0) {
                trayHelperTrace("prune dead tip session pid=" +
                                std::to_string(
                                    static_cast<unsigned long>(it->first)));
            }
            it = g_tipSessionRefCount.erase(it);
        } else {
            ++it;
        }
    }
}

void applyTrayServiceTipSessionEvent(
    const fcitx::TrayServiceTipSessionEvent &event) {
    const DWORD pid = event.processId;
    if (event.active != FALSE) {
        g_tipSessionRefCount[pid]++;
        return;
    }
    auto it = g_tipSessionRefCount.find(pid);
    if (it == g_tipSessionRefCount.end()) {
        return;
    }
    if (it->second > 1) {
        it->second--;
    } else {
        g_tipSessionRefCount.erase(it);
    }
}

void applyTrayServiceUi(const fcitx::TrayServiceUiEvent &ui) {
    g_trayState.hasUi = true;
    g_trayState.visible = ui.engineTrayVisible != FALSE;
}

void applyTrayServiceStatus(const fcitx::TrayServiceStatusEvent &status) {
    g_trayState.hasStatus = true;
    g_trayState.chineseMode = status.chineseMode != FALSE;
    g_trayState.currentInputMethod =
        fcitx::trayServiceUtf8FromBuffer(status.currentInputMethod);
    g_trayState.statusActions.clear();
    const UINT count =
        (status.actionCount > fcitx::kTrayServiceMaxStatusActionCount)
            ? static_cast<UINT>(fcitx::kTrayServiceMaxStatusActionCount)
            : status.actionCount;
    g_trayState.statusActions.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        TrayStatusActionItem item;
        item.uniqueName =
            fcitx::trayServiceUtf8FromBuffer(status.actions[i].uniqueName);
        item.displayName =
            fcitx::trayServiceWideFromBuffer(status.actions[i].displayName);
        item.isChecked = status.actions[i].isChecked != FALSE;
        if (!item.uniqueName.empty()) {
            g_trayState.statusActions.push_back(std::move(item));
        }
    }
}

void applyTrayServiceSnapshot(const fcitx::TrayServiceSnapshot &snapshot) {
    g_trayState.hasUi = true;
    g_trayState.hasStatus = true;
    g_trayState.visible = snapshot.visible != FALSE;
    g_trayState.chineseMode = snapshot.chineseMode != FALSE;
    g_trayState.currentInputMethod =
        fcitx::trayServiceUtf8FromBuffer(snapshot.currentInputMethod);
    g_trayState.statusActions.clear();
    const UINT count =
        (snapshot.actionCount > fcitx::kTrayServiceMaxStatusActionCount)
            ? static_cast<UINT>(fcitx::kTrayServiceMaxStatusActionCount)
            : snapshot.actionCount;
    g_trayState.statusActions.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        TrayStatusActionItem item;
        item.uniqueName =
            fcitx::trayServiceUtf8FromBuffer(snapshot.actions[i].uniqueName);
        item.displayName =
            fcitx::trayServiceWideFromBuffer(snapshot.actions[i].displayName);
        item.isChecked = snapshot.actions[i].isChecked != FALSE;
        if (!item.uniqueName.empty()) {
            g_trayState.statusActions.push_back(std::move(item));
        }
    }
}

std::string trayServiceCurrentInputMethodState() {
    return g_trayState.currentInputMethod;
}

bool trayServiceChineseModeState() {
    return g_trayState.hasStatus ? g_trayState.chineseMode : true;
}

std::vector<TrayStatusActionItem> trayServiceStatusActions() {
    return g_trayState.statusActions;
}

std::wstring trayStatusActionMenuText(const std::string &uniqueName,
                                      bool checked) {
    if (uniqueName == "punctuation") {
        return checked ? L"\x4e2d\x6587\x6807\x70b9\xff1a\x5f00"
                       : L"\x4e2d\x6587\x6807\x70b9\xff1a\x5173";
    }
    if (uniqueName == "fullwidth") {
        return checked ? L"\x5168\x89d2\x8f93\x5165\xff1a\x5f00"
                       : L"\x5168\x89d2\x8f93\x5165\xff1a\x5173";
    }
    if (uniqueName == "chttrans") {
        return checked ? L"\x7e41\x4f53\x4e2d\x6587\xff1a\x5f00"
                       : L"\x7e41\x4f53\x4e2d\x6587\xff1a\x5173";
    }
    return {};
}

std::wstring trayChineseModeSubmenuText(bool chineseMode) {
    return chineseMode ? L"\x8f93\x5165\x6a21\x5f0f(\x4e2d\x6587)"
                       : L"\x8f93\x5165\x6a21\x5f0f(\x82f1\x6587)";
}

std::wstring trayStatusActionSubmenuText(const std::string &uniqueName,
                                         bool checked) {
    if (uniqueName == "punctuation") {
        return checked ? L"\x4e2d\x82f1\x6587\x6807\x70b9(\x4e2d\x6587)"
                       : L"\x4e2d\x82f1\x6587\x6807\x70b9(\x82f1\x6587)";
    }
    if (uniqueName == "fullwidth") {
        return checked ? L"\x5168\x89d2/\x534a\x89d2(\x5168\x89d2)"
                       : L"\x5168\x89d2/\x534a\x89d2(\x534a\x89d2)";
    }
    if (uniqueName == "chttrans") {
        return checked ? L"\x5b57\x7b26\x96c6(\x7e41\x4f53)"
                       : L"\x5b57\x7b26\x96c6(\x7b80\x4f53)";
    }
    return {};
}

const TrayStatusActionItem *findTrayStatusAction(
    const std::vector<TrayStatusActionItem> &items, const char *uniqueName) {
    for (const auto &item : items) {
        if (item.uniqueName == uniqueName) {
            return &item;
        }
    }
    return nullptr;
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

std::string trayMenuCommandName(UINT cmd) {
    switch (cmd) {
    case 0:
        return "cancel";
    case IDM_INPUT_MODE_CHINESE:
        return "input_mode_chinese";
    case IDM_INPUT_MODE_ENGLISH:
        return "input_mode_english";
    case IDM_FULLWIDTH_ON:
        return "fullwidth_on";
    case IDM_FULLWIDTH_OFF:
        return "fullwidth_off";
    case IDM_SCRIPT_TRADITIONAL:
        return "script_traditional";
    case IDM_SCRIPT_SIMPLIFIED:
        return "script_simplified";
    case IDM_PUNCTUATION_CHINESE:
        return "punctuation_chinese";
    case IDM_PUNCTUATION_ENGLISH:
        return "punctuation_english";
    case IDM_OPEN_CONFIG_DIR:
        return "open_config_dir";
    case IDM_OPEN_LOG_DIR:
        return "open_log_dir";
    case IDM_RIME_DEPLOY:
        return "rime_deploy";
    case IDM_RIME_OPEN_USER_DIR:
        return "rime_open_user_dir";
    case IDM_RIME_OPEN_LOG_DIR:
        return "rime_open_log_dir";
    default:
        if (cmd >= IDM_INPUT_METHOD_BASE) {
            return "input_method_index";
        }
        if (cmd >= IDM_STATUS_ACTION_BASE) {
            return "status_action_index";
        }
        return "unknown";
    }
}

void persistSharedTrayStatusActions(
    const std::vector<TrayStatusActionItem> &items) {
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
    for (const auto &item : items) {
        out << item.uniqueName << '\t' << (item.isChecked ? '1' : '0') << '\t'
            << wideToUtf8(item.displayName) << '\n';
    }
}

void optimisticToggleSharedTrayStatusActionState(const std::string &uniqueName) {
    auto items = trayServiceStatusActions();
    bool changed = false;
    for (auto &item : items) {
        if (item.uniqueName != uniqueName) {
            continue;
        }
        item.isChecked = !item.isChecked;
        if (const auto label = trayStatusActionMenuText(item.uniqueName, item.isChecked);
            !label.empty()) {
            item.displayName = label;
        }
        changed = true;
        break;
    }
    if (changed) {
        g_trayState.hasStatus = true;
        g_trayState.statusActions = items;
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
    std::string current = trayServiceCurrentInputMethodState();
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

std::filesystem::path fcitx5RimeUserDir();

void exploreUserFcitxConfig() {
    openDirectoryInDetachedExplorer(appDataRoot().wstring());
}

void exploreUserRimeConfig() {
    const auto dir = appDataRoot() / L"rime";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    openDirectoryInDetachedExplorer(dir.wstring());
}

bool directoryHasEntries(const std::filesystem::path &dir) {
    std::error_code ec;
    return std::filesystem::exists(dir, ec) &&
           std::filesystem::is_directory(dir, ec) &&
           !std::filesystem::is_empty(dir, ec);
}

std::filesystem::path fcitx5LogDir() {
    auto dir = appDataRoot() / L"log";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path preferredFcitx5LogDir() {
    const auto logDir = fcitx5LogDir();
    const auto traceFile = appDataRoot() / L"tsf-trace.log";
    std::error_code ec;
    if (!directoryHasEntries(logDir) && std::filesystem::exists(traceFile, ec)) {
        return appDataRoot();
    }
    return logDir;
}

void exploreFcitx5LogDir() {
    const auto dir = preferredFcitx5LogDir();
    trayHelperTrace("exploreFcitx5LogDir path=" + dir.string());
    openDirectoryInDetachedExplorer(dir.wstring());
}

void exploreRimeLogDir() {
    auto dir = fcitx5RimeUserDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto sharedLogDir = fcitx5LogDir();
    if (directoryHasEntries(sharedLogDir)) {
        dir = sharedLogDir;
    }
    trayHelperTrace("exploreRimeLogDir path=" + dir.string());
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

bool deleteTrayIconWithIdentity(bool useGuid, const char *tag) {
    NOTIFYICONDATAW nid = {};
    fillShellTrayNidIdentity(&nid, useGuid);
    SetLastError(ERROR_SUCCESS);
    const BOOL deleted = Shell_NotifyIconW(NIM_DELETE, &nid);
    const DWORD gle = GetLastError();
    trayHelperTrace(std::string("removeTrayIcon delete ") + tag +
                    " useGuid=" + (useGuid ? "true" : "false") +
                    " deleted=" + (deleted ? "true" : "false") +
                    " gle=" + std::to_string(static_cast<unsigned long>(gle)));
    return deleted != FALSE;
}

void removeTrayIcon() {
    if (!g_trayAdded || !g_hwnd) {
        return;
    }
    trayHelperTrace(std::string("removeTrayIcon begin trayAdded=true useGuid=") +
                    (g_useGuidIdentity ? "true" : "false"));
    const bool primaryDeleted =
        deleteTrayIconWithIdentity(g_useGuidIdentity, "primary");
    const bool secondaryDeleted =
        deleteTrayIconWithIdentity(!g_useGuidIdentity, "secondary");
    trayHelperTrace(std::string("removeTrayIcon end primaryDeleted=") +
                    (primaryDeleted ? "true" : "false") +
                    " secondaryDeleted=" +
                    (secondaryDeleted ? "true" : "false"));
    g_trayAdded = false;
}

bool updateTrayTooltip(bool chineseMode) {
    if (!g_trayAdded || !g_hwnd) {
        return false;
    }
    NOTIFYICONDATAW nid = {};
    fillShellTrayNidIdentity(&nid, g_useGuidIdentity);
    nid.uFlags |= NIF_TIP | NIF_SHOWTIP | NIF_ICON;
    nid.hIcon = g_icon;
    if (chineseMode) {
        wcsncpy_s(nid.szTip,
                  L"Fcitx5 \x2014 \x4e2d\x6587\nShift / Ctrl+Space \x5207\x6362\x4e2d/\x82f1",
                  _TRUNCATE);
    } else {
        wcsncpy_s(nid.szTip,
                  L"Fcitx5 \x2014 English\nShift / Ctrl+Space \x5207\x6362\x4e2d/\x82f1",
                  _TRUNCATE);
    }
    if (Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        return true;
    }
    trayHelperTrace(std::string("updateTrayTooltip failed, re-adding icon gle=") +
                    std::to_string(static_cast<unsigned long>(GetLastError())));
    g_trayAdded = false;
    return false;
}

// Tray shows when at least one host has called ActivateEx (session ref > 0).
// **Ui**.visible is an extra engine-level allow (Explorer does not bind TIP).
// Foreground validity is tracked in **g_foregroundTrayValidity** for diagnostics;
// it does not gate visibility yet (would hide the icon when focus leaves TIP hosts).
bool shouldShowTrayIcon() {
    if (g_contextMenuShowing) {
        return true;
    }
    if (g_trayState.hasUi && !g_trayState.visible) {
        return false;
    }
    return !g_tipSessionRefCount.empty();
}

void refreshTrayState() {
    pruneInactiveTipSessions();
    updateForegroundTrayValidityState();
    const bool chineseMode = trayServiceChineseModeState();
    const std::string current = trayServiceCurrentInputMethodState();
    const bool visible = shouldShowTrayIcon();
    trayHelperTrace(
        "refreshTrayState begin visible=" + std::string(visible ? "true" : "false") +
        " trayAdded=" + std::string(g_trayAdded ? "true" : "false") +
        " useGuid=" + std::string(g_useGuidIdentity ? "true" : "false") +
        " chinese=" + std::string(chineseMode ? "true" : "false") +
        " current=" + current +
        " fgPid=" +
        std::to_string(
            static_cast<unsigned long>(g_foregroundTrayValidity.foregroundPid)) +
        " fgTip=" +
        std::string(g_foregroundTrayValidity.foregroundPidIsTipSession ? "true"
                                                                       : "false") +
        " fgShell=" +
        std::string(g_foregroundTrayValidity.foregroundIsShellOrHelper ? "true"
                                                                       : "false"));
    if (!visible) {
        if (g_trayAdded) {
            trayHelperTrace("refreshTrayState hiding tray icon current=" + current);
        }
        removeTrayIcon();
        g_lastChineseMode = chineseMode;
        g_lastCurrentIm = current;
        return;
    }
    if (!g_trayAdded) {
        addTrayIcon(chineseMode);
    } else if (chineseMode != g_lastChineseMode || current != g_lastCurrentIm) {
        if (!updateTrayTooltip(chineseMode)) {
            addTrayIcon(chineseMode);
        }
    }
    g_lastChineseMode = chineseMode;
    g_lastCurrentIm = current;
}

void queueDebouncedTrayRefresh() {
    if (!g_hwnd) {
        return;
    }
    KillTimer(g_hwnd, kTrayRefreshDebounceTimerId);
    SetTimer(g_hwnd, kTrayRefreshDebounceTimerId, kTrayRefreshDebounceMs,
             nullptr);
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

/// Foreground transfer for notification-icon menus; pairs with AttachThreadInput
/// to avoid fighting the shell (Explorer can host fcitx + MSCTF).
void bringTrayHelperWindowForContextMenu() {
    if (!g_hwnd) {
        return;
    }
    HWND fg = GetForegroundWindow();
    DWORD tidFg = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD tidCur = GetCurrentThreadId();
    if (tidFg != 0 && tidFg != tidCur) {
        AttachThreadInput(tidCur, tidFg, TRUE);
    }
    SetForegroundWindow(g_hwnd);
    if (tidFg != 0 && tidFg != tidCur) {
        AttachThreadInput(tidCur, tidFg, FALSE);
    }
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
    POINT pt = g_reopenMenuPending ? g_reopenMenuPoint : POINT{};
    g_reopenMenuPending = false;
    if (pt.x == 0 && pt.y == 0) {
        GetCursorPos(&pt);
    }
    {
        auto profileItems = readProfileInputMethodsFromConfig();
        auto statusActions = trayServiceStatusActions();
        const bool chineseMode = trayServiceChineseModeState();
        trayHelperTrace("showContextMenu chineseMode=" +
                        std::string(chineseMode ? "true" : "false") +
                        " statusActions=" +
                        std::to_string(static_cast<unsigned long>(
                            statusActions.size())) +
                        " profileItems=" +
                        std::to_string(static_cast<unsigned long>(
                            profileItems.size())));
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
        HMENU statusMenu = CreatePopupMenu();
        if (statusMenu) {
            HMENU inputModeMenu = CreatePopupMenu();
            if (inputModeMenu) {
                appendRadioMenuItem(inputModeMenu, IDM_INPUT_MODE_CHINESE,
                                    L"\x4e2d\x6587\x6a21\x5f0f");
                appendRadioMenuItem(inputModeMenu, IDM_INPUT_MODE_ENGLISH,
                                    L"\x82f1\x6587\x6a21\x5f0f");
                CheckMenuRadioItem(inputModeMenu, IDM_INPUT_MODE_CHINESE,
                                   IDM_INPUT_MODE_ENGLISH,
                                   chineseMode ? IDM_INPUT_MODE_CHINESE
                                               : IDM_INPUT_MODE_ENGLISH,
                                   MF_BYCOMMAND);
                AppendMenuW(statusMenu, MF_POPUP,
                            reinterpret_cast<UINT_PTR>(inputModeMenu),
                            trayChineseModeSubmenuText(chineseMode).c_str());
            }

            if (const auto *item = findTrayStatusAction(statusActions, "fullwidth")) {
                HMENU subMenu = CreatePopupMenu();
                if (subMenu) {
                    appendRadioMenuItem(subMenu, IDM_FULLWIDTH_ON,
                                        L"\x5168\x89d2");
                    appendRadioMenuItem(subMenu, IDM_FULLWIDTH_OFF,
                                        L"\x534a\x89d2");
                    CheckMenuRadioItem(subMenu, IDM_FULLWIDTH_ON, IDM_FULLWIDTH_OFF,
                                       item->isChecked ? IDM_FULLWIDTH_ON
                                                       : IDM_FULLWIDTH_OFF,
                                       MF_BYCOMMAND);
                    AppendMenuW(statusMenu, MF_POPUP,
                                reinterpret_cast<UINT_PTR>(subMenu),
                                trayStatusActionSubmenuText(item->uniqueName,
                                                            item->isChecked)
                                    .c_str());
                }
            }

            if (const auto *item = findTrayStatusAction(statusActions, "chttrans")) {
                HMENU subMenu = CreatePopupMenu();
                if (subMenu) {
                    appendRadioMenuItem(subMenu, IDM_SCRIPT_SIMPLIFIED,
                                        L"\x7b80\x4f53\x4e2d\x6587");
                    appendRadioMenuItem(subMenu, IDM_SCRIPT_TRADITIONAL,
                                        L"\x7e41\x4f53\x4e2d\x6587");
                    CheckMenuRadioItem(subMenu, IDM_SCRIPT_TRADITIONAL,
                                       IDM_SCRIPT_SIMPLIFIED,
                                       item->isChecked ? IDM_SCRIPT_TRADITIONAL
                                                       : IDM_SCRIPT_SIMPLIFIED,
                                       MF_BYCOMMAND);
                    AppendMenuW(statusMenu, MF_POPUP,
                                reinterpret_cast<UINT_PTR>(subMenu),
                                trayStatusActionSubmenuText(item->uniqueName,
                                                            item->isChecked)
                                    .c_str());
                }
            }

            if (const auto *item = findTrayStatusAction(statusActions, "punctuation")) {
                HMENU subMenu = CreatePopupMenu();
                if (subMenu) {
                    appendRadioMenuItem(subMenu, IDM_PUNCTUATION_CHINESE,
                                        L"\x4e2d\x6587\x6807\x70b9");
                    appendRadioMenuItem(subMenu, IDM_PUNCTUATION_ENGLISH,
                                        L"\x82f1\x6587\x6807\x70b9");
                    CheckMenuRadioItem(subMenu, IDM_PUNCTUATION_CHINESE,
                                       IDM_PUNCTUATION_ENGLISH,
                                       item->isChecked ? IDM_PUNCTUATION_CHINESE
                                                       : IDM_PUNCTUATION_ENGLISH,
                                       MF_BYCOMMAND);
                    AppendMenuW(statusMenu, MF_POPUP,
                                reinterpret_cast<UINT_PTR>(subMenu),
                                trayStatusActionSubmenuText(item->uniqueName,
                                                            item->isChecked)
                                    .c_str());
                }
            }
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(statusMenu),
                        L"\x8f93\x5165\x72b6\x6001");
        }

        HMENU imMenu = CreatePopupMenu();
        if (imMenu) {
            if (profileItems.empty()) {
                AppendMenuW(imMenu, MF_STRING | MF_GRAYED, IDM_INPUT_METHOD_BASE,
                            L"\x5f53\x524d profile \x4e2d\x6ca1\x6709\x53ef\x5207\x6362\x8f93\x5165\x6cd5");
            } else {
                for (size_t i = 0; i < profileItems.size(); ++i) {
                    AppendMenuW(imMenu,
                                MF_STRING |
                                    (profileItems[i].isCurrent ? MF_CHECKED : 0),
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
        AppendMenuW(menu, MF_STRING, IDM_OPEN_CONFIG_DIR,
                    L"\x6253\x5f00\x914d\x7f6e\x6587\x4ef6\x5939");
        AppendMenuW(menu, MF_STRING, IDM_OPEN_LOG_DIR,
                    L"\x6253\x5f00 Fcitx5 \x65e5\x5fd7\x76ee\x5f55");

        g_contextMenuShowing = true;
        bringTrayHelperWindowForContextMenu();
        const UINT cmd =
            TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD |
                                      TPM_NONOTIFY,
                           pt.x, pt.y, 0, g_hwnd, nullptr);
        g_contextMenuShowing = false;
        trayHelperTrace("showContextMenu selected cmd=" +
                        std::to_string(static_cast<unsigned long>(cmd)) + " (" +
                        trayMenuCommandName(cmd) + ")");
        PostMessageW(g_hwnd, WM_NULL, 0, 0);
        DestroyMenu(menu);

        bool reopenMenu = false;
        switch (cmd) {
        case 0:
            return;
        case IDM_INPUT_MODE_CHINESE:
            if (!chineseMode) {
                g_trayState.hasStatus = true;
                g_trayState.chineseMode = true;
                persistSharedTrayChineseModeRequest(true);
                persistSharedTrayChineseModeState(true);
            }
            refreshTrayState();
            reopenMenu = true;
            break;
        case IDM_INPUT_MODE_ENGLISH:
            if (chineseMode) {
                g_trayState.hasStatus = true;
                g_trayState.chineseMode = false;
                persistSharedTrayChineseModeRequest(false);
                persistSharedTrayChineseModeState(false);
            }
            refreshTrayState();
            reopenMenu = true;
            break;
        default:
            if (cmd == IDM_FULLWIDTH_ON || cmd == IDM_FULLWIDTH_OFF ||
                cmd == IDM_SCRIPT_SIMPLIFIED || cmd == IDM_SCRIPT_TRADITIONAL ||
                cmd == IDM_PUNCTUATION_CHINESE || cmd == IDM_PUNCTUATION_ENGLISH) {
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
                if (const auto *item = findTrayStatusAction(statusActions, actionName);
                    item && item->isChecked != targetChecked) {
                    persistSharedTrayStatusActionRequest(item->uniqueName);
                    optimisticToggleSharedTrayStatusActionState(item->uniqueName);
                }
                refreshTrayState();
                reopenMenu = true;
                break;
            }
            if (cmd >= IDM_INPUT_METHOD_BASE &&
                cmd < IDM_INPUT_METHOD_BASE + profileItems.size()) {
                const auto &item = profileItems[cmd - IDM_INPUT_METHOD_BASE];
                g_trayState.hasStatus = true;
                g_trayState.currentInputMethod = item.uniqueName;
                g_trayState.chineseMode = true;
                persistSharedTrayInputMethodRequest(item.uniqueName);
                persistSharedTrayCurrentInputMethodState(item.uniqueName);
                persistSharedTrayChineseModeRequest(true);
                persistSharedTrayChineseModeState(true);
                refreshTrayState();
                return;
            }
            break;
        case IDM_OPEN_CONFIG_DIR:
            exploreUserFcitxConfig();
            return;
        case IDM_OPEN_LOG_DIR:
            exploreFcitx5LogDir();
            return;
        case IDM_RIME_DEPLOY:
            launchRimeDeployWithNotification();
            return;
        case IDM_RIME_OPEN_USER_DIR:
            exploreUserRimeConfig();
            return;
        case IDM_RIME_OPEN_LOG_DIR:
            exploreRimeLogDir();
            return;
        }
        if (reopenMenu) {
            trayHelperTrace("showContextMenu reopen disabled for stability");
        }
        return;
    }
}

bool handleTrayServiceCopyData(const COPYDATASTRUCT *cds) {
    if (!cds || !cds->lpData) {
        return false;
    }
    if (cds->dwData == fcitx::kTrayServiceCopyDataSnapshot &&
        cds->cbData == sizeof(fcitx::TrayServiceSnapshot)) {
        const auto *snapshot =
            reinterpret_cast<const fcitx::TrayServiceSnapshot *>(cds->lpData);
        if (snapshot->version != 1) {
            return false;
        }
        applyTrayServiceSnapshot(*snapshot);
        trayHelperTrace("received tray snapshot snapshotVisible=" +
                        std::string(snapshot->visible != FALSE ? "true" : "false") +
                        " chinese=" +
                        std::string(g_trayState.chineseMode ? "true" : "false") +
                        " current=" + g_trayState.currentInputMethod);
        queueDebouncedTrayRefresh();
        return true;
    }
    if (cds->dwData == fcitx::kTrayServiceCopyDataUi &&
        cds->cbData == sizeof(fcitx::TrayServiceUiEvent)) {
        const auto *ui = reinterpret_cast<const fcitx::TrayServiceUiEvent *>(
            cds->lpData);
        if (ui->version != 1) {
            return false;
        }
        applyTrayServiceUi(*ui);
        trayHelperTrace("received tray ui engineVisible=" +
                        std::string(ui->engineTrayVisible != FALSE ? "true"
                                                                   : "false"));
        queueDebouncedTrayRefresh();
        return true;
    }
    if (cds->dwData == fcitx::kTrayServiceCopyDataStatus &&
        cds->cbData == sizeof(fcitx::TrayServiceStatusEvent)) {
        const auto *st = reinterpret_cast<const fcitx::TrayServiceStatusEvent *>(
            cds->lpData);
        if (st->version != 1) {
            return false;
        }
        applyTrayServiceStatus(*st);
        trayHelperTrace("received tray status chinese=" +
                        std::string(g_trayState.chineseMode ? "true" : "false") +
                        " current=" + g_trayState.currentInputMethod);
        queueDebouncedTrayRefresh();
        return true;
    }
    if (cds->dwData == fcitx::kTrayServiceCopyDataTipSession &&
        cds->cbData == sizeof(fcitx::TrayServiceTipSessionEvent)) {
        const auto *event =
            reinterpret_cast<const fcitx::TrayServiceTipSessionEvent *>(cds->lpData);
        if (event->version != 1) {
            return false;
        }
        applyTrayServiceTipSessionEvent(*event);
        trayHelperTrace(
            "received focus/tip session active=" +
            std::string(event->active != FALSE ? "true" : "false") + " pid=" +
            std::to_string(static_cast<unsigned long>(event->processId)));
        queueDebouncedTrayRefresh();
        return true;
    }
    return false;
}

LRESULT CALLBACK trayHelperWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kShellTrayCallback) {
        const UINT code = LOWORD(static_cast<DWORD_PTR>(lp));
        if (code == WM_LBUTTONUP || code == NIN_SELECT || code == NIN_KEYSELECT) {
            const bool chineseMode = trayServiceChineseModeState();
            g_trayState.hasStatus = true;
            g_trayState.chineseMode = !chineseMode;
            persistSharedTrayChineseModeRequest(!chineseMode);
            persistSharedTrayChineseModeState(!chineseMode);
            refreshTrayState();
        } else if (code == WM_RBUTTONUP || code == WM_CONTEXTMENU) {
            // Shell often delivers both WM_RBUTTONUP and WM_CONTEXTMENU for one
            // click; running showContextMenu twice can steal foreground twice and
            // destabilize Explorer (fcitx + MSCTF in explorer.exe).
            static ULONGLONG s_lastContextMenuTick = 0;
            const ULONGLONG now = GetTickCount64();
            if (now - s_lastContextMenuTick < 400ULL) {
                trayHelperTrace(
                    "tray context menu debounced duplicate notify code=" +
                    std::to_string(static_cast<unsigned long>(code)));
                return 0;
            }
            s_lastContextMenuTick = now;
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
    case WM_COPYDATA:
        if (handleTrayServiceCopyData(reinterpret_cast<const COPYDATASTRUCT *>(lp))) {
            return TRUE;
        }
        return FALSE;
    case WM_CREATE:
        trayHelperTrace("wndproc WM_CREATE");
        refreshTrayState();
        SetTimer(hwnd, kPeriodicTipSessionTimerId, kPeriodicTipSessionMs, nullptr);
        return 0;
    case WM_TIMER:
        if (wp == kRetryTimerId) {
            g_retryPending = false;
            KillTimer(hwnd, kRetryTimerId);
            refreshTrayState();
            return 0;
        }
        if (wp == kTrayRefreshDebounceTimerId) {
            KillTimer(hwnd, kTrayRefreshDebounceTimerId);
            refreshTrayState();
            return 0;
        }
        if (wp == kPeriodicTipSessionTimerId) {
            refreshTrayState();
            return 0;
        }
        break;
    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
    case WM_THEMECHANGED:
        if (!g_trayAdded) {
            refreshTrayState();
        }
        return 0;
    case WM_DESTROY:
        trayHelperTrace("wndproc WM_DESTROY");
        KillTimer(hwnd, kRetryTimerId);
        KillTimer(hwnd, kTrayRefreshDebounceTimerId);
        KillTimer(hwnd, kPeriodicTipSessionTimerId);
        removeTrayIcon();
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        trayHelperTrace("wndproc WM_CLOSE");
        break;
    case WM_QUERYENDSESSION:
        trayHelperTrace("wndproc WM_QUERYENDSESSION");
        break;
    case WM_ENDSESSION:
        trayHelperTrace("wndproc WM_ENDSESSION");
        break;
    case WM_NCDESTROY:
        trayHelperTrace("wndproc WM_NCDESTROY");
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    installTrayHelperCrashLogging();
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
    wc.lpszClassName = fcitx::kStandaloneTrayHelperWindowClass;
    const ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (g_iconOwned && g_icon) {
            DestroyIcon(g_icon);
        }
        CloseHandle(g_singleInstanceMutex);
        return 1;
    }

    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW,
                             fcitx::kStandaloneTrayHelperWindowClass, L"",
                             WS_POPUP,
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
