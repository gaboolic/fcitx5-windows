#pragma once

#include <Windows.h>

#include <cstddef>
#include <string>

namespace fcitx {

constexpr wchar_t kStandaloneTrayHelperWindowClass[] =
    L"Fcitx5StandaloneTrayHelperWindow";
constexpr ULONG_PTR kTrayServiceCopyDataSnapshot = 0x46545331ULL;
/// Per-process TSF TIP ActivateEx/Deactivate (not document focus / OnSetFocus).
constexpr ULONG_PTR kTrayServiceCopyDataTipSession = 0x46545333ULL;
constexpr size_t kTrayServiceMaxInputMethodLength = 64;
constexpr size_t kTrayServiceMaxStatusActionCount = 8;
constexpr size_t kTrayServiceMaxStatusActionNameLength = 32;
constexpr size_t kTrayServiceMaxStatusActionDisplayNameLength = 64;

struct TrayServiceStatusActionState {
    char uniqueName[kTrayServiceMaxStatusActionNameLength];
    wchar_t displayName[kTrayServiceMaxStatusActionDisplayNameLength];
    BOOL isChecked;
};

struct TrayServiceTipSessionEvent {
    DWORD version;
    DWORD processId;
    BOOL active;
};

struct TrayServiceSnapshot {
    DWORD version;
    BOOL visible;
    BOOL chineseMode;
    char currentInputMethod[kTrayServiceMaxInputMethodLength];
    UINT actionCount;
    TrayServiceStatusActionState actions[kTrayServiceMaxStatusActionCount];
};

template <size_t N>
inline void trayServiceCopyUtf8(char (&dest)[N], const std::string &src) {
    size_t i = 0;
    for (; i + 1 < N && i < src.size(); ++i) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
    for (++i; i < N; ++i) {
        dest[i] = '\0';
    }
}

template <size_t N>
inline void trayServiceCopyWide(wchar_t (&dest)[N], const std::wstring &src) {
    size_t i = 0;
    for (; i + 1 < N && i < src.size(); ++i) {
        dest[i] = src[i];
    }
    dest[i] = L'\0';
    for (++i; i < N; ++i) {
        dest[i] = L'\0';
    }
}

template <size_t N>
inline std::string trayServiceUtf8FromBuffer(const char (&src)[N]) {
    size_t len = 0;
    while (len < N && src[len] != '\0') {
        ++len;
    }
    return std::string(src, src + len);
}

template <size_t N>
inline std::wstring trayServiceWideFromBuffer(const wchar_t (&src)[N]) {
    size_t len = 0;
    while (len < N && src[len] != L'\0') {
        ++len;
    }
    return std::wstring(src, src + len);
}

inline HWND findStandaloneTrayHelperWindow() {
    return FindWindowW(kStandaloneTrayHelperWindowClass, nullptr);
}

inline bool sendTrayServiceCopyData(HWND hwnd, ULONG_PTR kind, const void *data,
                                    DWORD sizeBytes) {
    if (!hwnd || !data || sizeBytes == 0) {
        return false;
    }
    COPYDATASTRUCT cds = {};
    cds.dwData = kind;
    cds.cbData = sizeBytes;
    cds.lpData = const_cast<void *>(data);
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(hwnd, WM_COPYDATA, 0,
                               reinterpret_cast<LPARAM>(&cds),
                               SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result) != 0;
}

} // namespace fcitx
