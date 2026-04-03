#include "register.h"
#include "util.h"
#include <msctf.h>
#include <format>
#include <wrl/client.h>

#define FCITX5 "Fcitx5"
#define THREADING_MODEL "ThreadingModel"
#define APARTMENT "Apartment"

namespace fcitx {
HINSTANCE dllInstance; // Set by DllMain.

void RegisterTrace(const std::string &message) {
    const std::string line = message + "\r\n";
    const auto appendLine = [&](const std::wstring &file) {
        HANDLE h = CreateFileW(file.c_str(), FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            return;
        }
        DWORD written = 0;
        WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &written,
                  nullptr);
        CloseHandle(h);
    };

    WCHAR value[MAX_PATH];
    const DWORD appDataLen = GetEnvironmentVariableW(L"APPDATA", value, MAX_PATH);
    if (appDataLen != 0 && appDataLen < MAX_PATH) {
        const std::wstring dir = std::wstring(value) + L"\\Fcitx5";
        CreateDirectoryW(dir.c_str(), nullptr);
        appendLine(dir + L"\\register-trace.log");
    }

    const DWORD programDataLen =
        GetEnvironmentVariableW(L"PROGRAMDATA", value, MAX_PATH);
    if (programDataLen != 0 && programDataLen < MAX_PATH) {
        const std::wstring dir = std::wstring(value) + L"\\Fcitx5";
        CreateDirectoryW(dir.c_str(), nullptr);
        appendLine(dir + L"\\register-trace.log");
    }

    WCHAR dllPath[MAX_PATH];
    if (GetModuleFileNameW(dllInstance, dllPath, MAX_PATH) != 0) {
        std::wstring dir(dllPath);
        const size_t pos = dir.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            dir.resize(pos);
            appendLine(dir + L"\\register-trace.log");
        }
    }
}

namespace {

HKL findImeKeyboardLayoutForLang(LANGID langid) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts", 0,
                      KEY_READ, &hKey) != ERROR_SUCCESS) {
        return nullptr;
    }
    WCHAR keyName[KL_NAMELENGTH] = {};
    // Align with Weasel: only treat IME-style E0xx HKLs as substitute layouts.
    // Binding to the generic 0000xxxx keyboard layout causes TSF to emit a
    // SubstituteLayout entry that does not correspond to a dedicated TIP/IME.
    for (DWORD value = (0xE0200000u | langid);
         value <= (0xE0FF0000u | langid); value += 0x10000u) {
        if (swprintf(keyName, ARRAYSIZE(keyName), L"%08X", value) < 0) {
            continue;
        }
        HKEY hSubKey = nullptr;
        if (RegOpenKeyExW(hKey, keyName, 0, KEY_READ, &hSubKey) ==
            ERROR_SUCCESS) {
            RegCloseKey(hSubKey);
            RegCloseKey(hKey);
            return reinterpret_cast<HKL>(static_cast<ULONG_PTR>(value));
        }
    }
    RegCloseKey(hKey);
    return nullptr;
}

} // namespace

/*
HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}: Fcitx5
  - InprocServer32: C:\Windows\system32
    ThreadingModel: Apartment
*/
BOOL RegisterServer() {
    DWORD dw;
    HKEY hKey = nullptr;
    HKEY hSubKey = nullptr;
    WCHAR dllPath[MAX_PATH];
    auto achIMEKey = "SOFTWARE\\Classes\\CLSID\\" + guidToString(FCITX_CLSID);
    const LSTATUS status = RegCreateKeyExA(HKEY_LOCAL_MACHINE, achIMEKey.c_str(),
                                           0, nullptr, REG_OPTION_NON_VOLATILE,
                                           KEY_WRITE, nullptr, &hKey, &dw);
    RegisterTrace(std::format("RegisterServer RegCreateKeyExA status={}",
                              static_cast<long>(status)));
    if (status != ERROR_SUCCESS) {
        return FALSE;
    }
    LSTATUS ret = RegSetValueExA(hKey, nullptr, 0, REG_SZ,
                                 reinterpret_cast<const BYTE *>(FCITX5),
                                 sizeof FCITX5);
    RegisterTrace(std::format("RegisterServer RegSetValueExA(name) status={}",
                              static_cast<long>(ret)));
    if (ret != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return FALSE;
    }
    ret = RegCreateKeyExA(hKey, "InprocServer32", 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hSubKey,
                          &dw);
    RegisterTrace(std::format(
        "RegisterServer RegCreateKeyExA(InprocServer32) status={}",
        static_cast<long>(ret)));
    if (ret != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return FALSE;
    }
    const auto hr = GetModuleFileNameW(dllInstance, dllPath, MAX_PATH);
    RegisterTrace(std::format("RegisterServer dllPathLen={}",
                              static_cast<unsigned long>(hr)));
    ret = RegSetValueExW(hSubKey, nullptr, 0, REG_SZ,
                         reinterpret_cast<const BYTE *>(dllPath),
                         hr * sizeof(WCHAR));
    RegisterTrace(std::format("RegisterServer RegSetValueExW(path) status={}",
                              static_cast<long>(ret)));
    if (ret != ERROR_SUCCESS) {
        RegCloseKey(hSubKey);
        RegCloseKey(hKey);
        return FALSE;
    }
    ret = RegSetValueExA(hSubKey, THREADING_MODEL, 0, REG_SZ,
                         reinterpret_cast<const BYTE *>(APARTMENT),
                         sizeof APARTMENT);
    RegisterTrace(std::format(
        "RegisterServer RegSetValueExA(ThreadingModel) status={}",
        static_cast<long>(ret)));
    RegCloseKey(hSubKey);
    RegCloseKey(hKey);
    return ret == ERROR_SUCCESS;
}

void UnregisterServer() {
    auto achIMEKey = "SOFTWARE\\Classes\\CLSID\\" + guidToString(FCITX_CLSID);
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, achIMEKey.c_str());
}

/*
HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\CTF\TIP\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}
  - LanguageProfile
    - 0x00000804
      - {9A92B895-29B9-4F19-9627-9F626C9490F2}
        Description: Fcitx5
        Enable: 0x00000001
        IconFile: current IME dll path (module resource icon)
        IconIndex: 0x00000000
*/
BOOL RegisterProfiles() {
    std::wstring pchDesc = stringToWString(FCITX5, CP_UTF8);
    WCHAR dllPath[MAX_PATH];
    const DWORD dllPathLen = GetModuleFileNameW(dllInstance, dllPath, MAX_PATH);
    if (dllPathLen == 0 || dllPathLen >= MAX_PATH) {
        return FALSE;
    }
    Microsoft::WRL::ComPtr<ITfInputProcessorProfileMgr> mgr;
    HRESULT hrCo = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                    CLSCTX_ALL, IID_PPV_ARGS(&mgr));
    RegisterTrace(std::format("RegisterProfiles CoCreateInstance hr=0x{:08X}",
                              static_cast<unsigned long>(hrCo)));
    if (FAILED(hrCo)) {
        return FALSE;
    }
    const auto registerProfile = [&](LANGID langid, HKL hkl, BOOL enable) {
        const HRESULT hr = mgr->RegisterProfile(
            FCITX_CLSID, langid, PROFILE_GUID, pchDesc.c_str(),
            pchDesc.size() * sizeof(WCHAR), dllPath, dllPathLen,
            0, hkl, 0, enable, 0);
        RegisterTrace(std::format(
            "RegisterProfiles lang=0x{:04X} hkl=0x{:08X} enable={} hr=0x{:08X}",
            static_cast<unsigned>(langid),
            static_cast<unsigned>(reinterpret_cast<ULONG_PTR>(hkl)),
            enable ? 1 : 0, static_cast<unsigned long>(hr)));
        return hr;
    };

    HRESULT hr = S_OK;
    hr = registerProfile(TEXTSERVICE_LANGID_HANS,
                         findImeKeyboardLayoutForLang(TEXTSERVICE_LANGID_HANS),
                         TRUE);
    if (FAILED(hr)) {
        return FALSE;
    }
    hr = registerProfile(TEXTSERVICE_LANGID_HANT,
                         findImeKeyboardLayoutForLang(TEXTSERVICE_LANGID_HANT),
                         FALSE);
    if (FAILED(hr)) {
        return FALSE;
    }
    hr = registerProfile(TEXTSERVICE_LANGID_HONGKONG, nullptr, FALSE);
    if (FAILED(hr)) {
        return FALSE;
    }
    hr = registerProfile(TEXTSERVICE_LANGID_MACAU, nullptr, FALSE);
    if (FAILED(hr)) {
        return FALSE;
    }
    hr = registerProfile(TEXTSERVICE_LANGID_SINGAPORE, nullptr, FALSE);
    return hr == S_OK;
}

// TSF category GUIDs (RegisterCategory). MinGW WIDL headers declare only a
// subset; spell out the rest (values match Windows SDK / msctf.idl).
// GUID_TFCAT_PROPSTYLE_CUSTOM is omitted here when it matches
// GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT (25504fb4-...) so RegisterCategory is not
// called twice with the same id (second call can fail with
// TF_E_ALREADY_EXISTS).
#if defined(__MINGW32__) || defined(__MSYS__)
namespace {
const GUID kRegisterTipCategories[] = {
    {0x534c48c1,
     0x0607,
     0x4098,
     {0xa5, 0x21, 0x4f, 0xc8, 0x99, 0xc7, 0x3e, 0x90}},
    {0x34745c63,
     0xb2f0,
     0x4784,
     {0x8b, 0x67, 0x5e, 0x12, 0xc8, 0x70, 0x1a, 0x31}},
    {0x49d2f9ce,
     0x1f5e,
     0x11d7,
     {0xa6, 0xd3, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}},
    {0x49d2f9cf,
     0x1f5e,
     0x11d7,
     {0xa6, 0xd3, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}},
    {0xccf05dd7,
     0x4a87,
     0x11d7,
     {0xa6, 0xe2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}},
    {0x364215d9,
     0x75bc,
     0x11d7,
     {0xa6, 0xef, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}},
    {0x364215da,
     0x75bc,
     0x11d7,
     {0xa6, 0xef, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}},
    {0x13a016df,
     0x560b,
     0x46cd,
     {0x94, 0x7a, 0x4c, 0x3a, 0xf1, 0xe0, 0xe3, 0x5d}},
    // Present in the working Weasel installation on Win11. Microsoft headers in
    // our toolchain do not name this newer TIP capability GUID, but without it
    // fcitx's installed Category tree differs from Weasel by exactly one item.
    {0x24af3031,
     0x852d,
     0x40a2,
     {0xbc, 0x09, 0x89, 0x92, 0x89, 0x8c, 0xe7, 0x22}},
    {0x25504fb4,
     0x7bab,
     0x4bc1,
     {0x9c, 0x69, 0xcf, 0x81, 0x89, 0x0f, 0x0e, 0xf5}},
    {0x9b7be3a9,
     0xe8ab,
     0x4d47,
     {0xa8, 0xfe, 0x25, 0x4f, 0xa4, 0x23, 0x43, 0x6d}},
    {0x7c6a82ae,
     0xb0d7,
     0x4f14,
     {0xa7, 0x45, 0x14, 0xf2, 0x8b, 0x00, 0x9d, 0x61}},
    {0x565fb8d8,
     0x6bd4,
     0x4ca1,
     {0xb2, 0x23, 0x0f, 0x2c, 0xcb, 0x8f, 0x4f, 0x96}},
    {0x85f9794b,
     0x4d19,
     0x40d8,
     {0x88, 0x64, 0x4e, 0x74, 0x73, 0x71, 0xa6, 0x6d}},
    {0x046b8c80,
     0x1647,
     0x40f7,
     {0x9b, 0x21, 0xb9, 0x3b, 0x81, 0xaa, 0xbc, 0x1b}},
    {0xb95f181b,
     0xea4c,
     0x4af1,
     {0x80, 0x56, 0x7c, 0x32, 0x1a, 0xbb, 0xb0, 0x91}},
};
}
#else
// No documentation about what they means.
const GUID Categories[] = {GUID_TFCAT_CATEGORY_OF_TIP,
                           GUID_TFCAT_TIP_KEYBOARD,
                           GUID_TFCAT_TIPCAP_SECUREMODE,
                           GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
                           GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
                           GUID_TFCAT_TIPCAP_COMLESS,
                           GUID_TFCAT_TIPCAP_WOW16,
                           GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
                           GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
                           GUID_TFCAT_PROP_AUDIODATA,
                           GUID_TFCAT_PROP_INKDATA,
                           GUID_TFCAT_PROPSTYLE_CUSTOM,
                           GUID_TFCAT_PROPSTYLE_STATIC,
                           GUID_TFCAT_PROPSTYLE_STATICCOMPACT,
                           GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
                           GUID_TFCAT_DISPLAYATTRIBUTEPROPERTY};
#endif

/*
HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\CTF\TIP\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}
  - Category
    - Category
      - GUID of categories
        - {FC3869BA-51E3-4078-8EE2-5FE49493A1F4}
      - ...
    - Item
      - {FC3869BA-51E3-4078-8EE2-5FE49493A1F4}
        - GUID of categories
        - ...
*/
BOOL RegisterCategories() {
    ITfCategoryMgr *mgr;
    const HRESULT createHr =
        CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                         IID_ITfCategoryMgr, reinterpret_cast<void **>(&mgr));
    RegisterTrace(std::format("RegisterCategories CoCreateInstance hr=0x{:08X}",
                              static_cast<unsigned long>(createHr)));
    if (FAILED(createHr)) {
        return FALSE;
    }
    HRESULT hr = S_OK;
#if defined(__MINGW32__) || defined(__MSYS__)
    for (const auto &guid : kRegisterTipCategories) {
#else
    for (const auto &guid : Categories) {
#endif
        const HRESULT catHr = mgr->RegisterCategory(FCITX_CLSID, guid, FCITX_CLSID);
        RegisterTrace(std::format("RegisterCategories guid={} hr=0x{:08X}",
                                  guidToString(guid),
                                  static_cast<unsigned long>(catHr)));
        hr |= catHr;
    }
    mgr->Release();
    return hr == S_OK;
}

void UnregisterCategoriesAndProfiles() {
    auto key = "SOFTWARE\\Microsoft\\CTF\\TIP\\" + guidToString(FCITX_CLSID);
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, key.c_str());
}
} // namespace fcitx
