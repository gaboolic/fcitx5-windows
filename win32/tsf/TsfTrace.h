#pragma once

#include "Win32GnuApiCompat.h"

#include <Windows.h>
#include <filesystem>
#include <sstream>
#include <string>

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

inline std::wstring currentProcessExeBaseName() {
    WCHAR exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        return {};
    }
    return std::filesystem::path(exePath).filename().wstring();
}

inline std::string currentProcessExeBaseNameUtf8() {
    const std::wstring baseName = currentProcessExeBaseName();
    return std::string(baseName.begin(), baseName.end());
}

inline void tsfTrace(const std::string &message) {
    const auto path = tsfTraceLogPath();
    const std::wstring wpath = pathAsWide(path);
    HANDLE file = CreateFileW(wpath.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    std::ostringstream ss;
    ss << "[pid=" << GetCurrentProcessId()
       << " process=" << currentProcessExeBaseNameUtf8() << "] " << message
       << "\r\n";
    const std::string line = ss.str();
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written,
              nullptr);
    CloseHandle(file);
}

inline bool currentProcessExeBaseNameEquals(const wchar_t *expected) {
    const std::wstring baseName = currentProcessExeBaseName();
    return !baseName.empty() && expected &&
           wideStringCompareI(baseName.c_str(), expected) == 0;
}

inline bool currentProcessIsStandaloneTrayHelper() {
    return currentProcessExeBaseNameEquals(L"fcitx5-tray-helper.exe");
}

inline bool currentProcessUsesMinimalTsfMode() {
    return currentProcessExeBaseNameEquals(L"explorer.exe") ||
           currentProcessIsStandaloneTrayHelper();
}

} // namespace fcitx
