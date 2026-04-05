#include "Fcitx5ImeIpcProtocol.h"

#include <Lmcons.h>

namespace fcitx {

std::wstring imeIpcPipeServerUserName() {
    wchar_t user[UNLEN + 1] = {};
    DWORD n = UNLEN + 1;
    if (!GetUserNameW(user, &n)) {
        user[0] = L'\0';
    }
    return user;
}

std::wstring imeIpcNamedPipePath() {
    std::wstring path = L"\\\\.\\pipe\\";
    path += imeIpcPipeServerUserName();
    path += L"\\";
    path += kImeIpcPipeBaseName;
    return path;
}

std::wstring imeIpcPipeServerSingletonMutexName() {
    std::wstring name = L"Local\\Fcitx5ImePipeServerSingleton_";
    name += imeIpcPipeServerUserName();
    return name;
}

std::wstring imeIpcPipeServerLaunchMutexName() {
    std::wstring name = L"Local\\Fcitx5ImePipeServerLaunch_";
    name += imeIpcPipeServerUserName();
    return name;
}

bool imeIpcReadAll(HANDLE h, void *buf, size_t size) {
    auto *p = static_cast<std::uint8_t *>(buf);
    size_t got = 0;
    while (got < size) {
        DWORD chunk = 0;
        if (!ReadFile(h, p + got, static_cast<DWORD>(size - got), &chunk,
                      nullptr) ||
            chunk == 0) {
            return false;
        }
        got += chunk;
    }
    return true;
}

bool imeIpcWriteAll(HANDLE h, const void *buf, size_t size) {
    const auto *p = static_cast<const std::uint8_t *>(buf);
    size_t put = 0;
    while (put < size) {
        DWORD chunk = 0;
        if (!WriteFile(h, p + put, static_cast<DWORD>(size - put), &chunk,
                       nullptr) ||
            chunk == 0) {
            return false;
        }
        put += chunk;
    }
    return true;
}

} // namespace fcitx
