#pragma once

#include <Windows.h>
#include <string>

#define TEXTSERVICE_LANGID_HANS                                                \
    MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)
#define TEXTSERVICE_LANGID_HANT                                                \
    MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)
#define TEXTSERVICE_LANGID_HONGKONG                                            \
    MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_HONGKONG)
#define TEXTSERVICE_LANGID_MACAU MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_MACAU)
#define TEXTSERVICE_LANGID_SINGAPORE                                           \
    MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SINGAPORE)

namespace fcitx {
extern const GUID FCITX_CLSID;
extern const GUID PROFILE_GUID;
std::string guidToString(REFGUID guid);
std::wstring stringToWString(const std::string &str, int codePage);
} // namespace fcitx
