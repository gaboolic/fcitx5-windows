#pragma once

#if (defined(__MSYS__) || defined(__CYGWIN__)) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

// MSVC-specific wide CRT helpers (_wcsicmp, wcsncpy_s, _wgetenv) and
// narrow std::filesystem::path vs LPCWSTR mismatches: MSYS2 /usr/bin/g++
// targets Windows via w32api with libstdc++ (char paths) and no UCRT-style
// secure wides. Use Win32 APIs + portable fallbacks.

#include <Windows.h>

#include <cwchar>
#include <filesystem>
#include <iterator>
#include <string>

#include <wchar.h>

namespace fcitx {

inline int wideStringCompareI(const wchar_t *a, const wchar_t *b) {
    if (!a || !b) {
        return (a == b) ? 0 : (a ? 1 : -1);
    }
#if defined(_MSC_VER)
    return _wcsicmp(a, b);
#elif defined(__MSYS__) || defined(__CYGWIN__)
    return wcscasecmp(a, b);
#else
    return _wcsicmp(a, b);
#endif
}

/// Copy \p src into a fixed wchar_t buffer; truncate with a trailing NUL.
inline void wideStringCopyTruncate(wchar_t *dest, size_t destChars,
                                   const wchar_t *src) {
    if (!dest || destChars == 0) {
        return;
    }
    if (!src) {
        dest[0] = L'\0';
        return;
    }
#if defined(_MSC_VER)
    wcsncpy_s(dest, destChars, src, _TRUNCATE);
#else
    size_t i = 0;
    for (; i + 1 < destChars && src[i]; ++i) {
        dest[i] = src[i];
    }
    dest[i] = L'\0';
#endif
}

inline std::wstring getEnvironmentVariableWide(const wchar_t *name) {
    if (!name || !*name) {
        return {};
    }
    wchar_t buf[32767];
    const DWORD n =
        GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) {
        return {};
    }
    return std::wstring(buf, n);
}

/// Wide Win32 path for APIs that take LPCWSTR (path::c_str() may be char* on
/// GNU libstdc++ even when built from wchar_t segments).
inline std::wstring pathAsWide(const std::filesystem::path &p) {
    return p.wstring();
}

} // namespace fcitx
