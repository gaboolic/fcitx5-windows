#include "register.h"
#include "util.h"
#include <filesystem>
#include <msctf.h>
#include <wrl/client.h>

namespace fs = std::filesystem;

#define FCITX5 "Fcitx5"
#define THREADING_MODEL "ThreadingModel"
#define APARTMENT "Apartment"

namespace fcitx {
HINSTANCE dllInstance; // Set by DllMain.

/*
HKEY_CLASSES_ROOT\CLSID\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}: Fcitx5
  - InprocServer32: C:\Windows\system32
    ThreadingModel: Apartment
*/
BOOL RegisterServer() {
    DWORD dw;
    HKEY hKey = nullptr;
    HKEY hSubKey = nullptr;
    WCHAR dllPath[MAX_PATH];
    auto achIMEKey = "CLSID\\" + guidToString(FCITX_CLSID);
    BOOL ret = RegCreateKeyExA(HKEY_CLASSES_ROOT, achIMEKey.c_str(), 0, nullptr,
                               REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                               &hKey, &dw);
    ret |=
        RegSetValueExA(hKey, nullptr, 0, REG_SZ,
                       reinterpret_cast<const BYTE *>(FCITX5), sizeof FCITX5);
    ret |= RegCreateKeyExA(hKey, "InprocServer32", 0, nullptr,
                           REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                           &hSubKey, &dw);
    auto hr = GetModuleFileNameW(dllInstance, dllPath, MAX_PATH);
    ret |= RegSetValueExW(hSubKey, nullptr, 0, REG_SZ,
                          reinterpret_cast<const BYTE *>(dllPath),
                          hr * sizeof(WCHAR));
    ret |= RegSetValueExA(hSubKey, THREADING_MODEL, 0, REG_SZ,
                          reinterpret_cast<const BYTE *>(APARTMENT),
                          sizeof APARTMENT);
    RegCloseKey(hSubKey);
    RegCloseKey(hKey);
    return ret == ERROR_SUCCESS;
}

void UnregisterServer() {
    auto achIMEKey = "CLSID\\" + guidToString(FCITX_CLSID);
    RegDeleteTreeA(HKEY_CLASSES_ROOT, achIMEKey.c_str());
}

/*
HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\CTF\TIP\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}
  - LanguageProfile
    - 0x00000804
      - {9A92B895-29B9-4F19-9627-9F626C9490F2}
        Description: Fcitx5
        Enable: 0x00000001
        IconFile: /path/to/icon in the same directory with dll
        IconIndex: 0x00000000
*/
BOOL RegisterProfiles() {
    std::wstring pchDesc = stringToWString(FCITX5, CP_UTF8);
    WCHAR dllPath[MAX_PATH];
    GetModuleFileNameW(dllInstance, dllPath, MAX_PATH);
    fs::path path = dllPath;
    path = path.remove_filename().append("penguin.ico");
    Microsoft::WRL::ComPtr<ITfInputProcessorProfileMgr> mgr;
    HRESULT hrCo = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                    CLSCTX_ALL, IID_PPV_ARGS(&mgr));
    if (FAILED(hrCo)) {
        return FALSE;
    }
    auto hr = mgr->RegisterProfile(
        FCITX_CLSID, TEXTSERVICE_LANGID_HANS, PROFILE_GUID, pchDesc.c_str(),
        pchDesc.size() * sizeof(WCHAR), path.c_str(),
        path.wstring().size() * sizeof(WCHAR), 0, nullptr, 0, 1, 0);
    return hr == S_OK;
}

// TSF category GUIDs (RegisterCategory). MinGW WIDL headers declare only a subset; spell out the
// rest (values match Windows SDK / msctf.idl). GUID_TFCAT_PROPSTYLE_CUSTOM is omitted here when it
// matches GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT (25504fb4-...) so RegisterCategory is not called twice
// with the same id (second call can fail with TF_E_ALREADY_EXISTS).
#if defined(__MINGW32__)
namespace {
const GUID kRegisterTipCategories[] = {
    {0x534c48c1, 0x0607, 0x4098, {0xa5, 0x21, 0x4f, 0xc8, 0x99, 0xc7, 0x3e, 0x90}},
    {0x34745c63, 0xb2f0, 0x4784, {0x8b, 0x67, 0x5e, 0x12, 0xc8, 0x70, 0x1a, 0x31}},
    {0x49d2f9ce, 0x1f5e, 0x11d7, {0xa6, 0xd3, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}},
    {0x49d2f9cf, 0x1f5e, 0x11d7, {0xa6, 0xd3, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}},
    {0xccf05dd7, 0x4a87, 0x11d7, {0xa6, 0xe2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}},
    {0x364215d9, 0x75bc, 0x11d7, {0xa6, 0xef, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}},
    {0x364215da, 0x75bc, 0x11d7, {0xa6, 0xef, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}},
    {0x13a016df, 0x560b, 0x46cd, {0x94, 0x7a, 0x4c, 0x3a, 0xf1, 0xe0, 0xe3, 0x5d}},
    {0x25504fb4, 0x7bab, 0x4bc1, {0x9c, 0x69, 0xcf, 0x81, 0x89, 0x0f, 0x0e, 0xf5}},
    {0x9b7be3a9, 0xe8ab, 0x4d47, {0xa8, 0xfe, 0x25, 0x4f, 0xa4, 0x23, 0x43, 0x6d}},
    {0x7c6a82ae, 0xb0d7, 0x4f14, {0xa7, 0x45, 0x14, 0xf2, 0x8b, 0x00, 0x9d, 0x61}},
    {0x565fb8d8, 0x6bd4, 0x4ca1, {0xb2, 0x23, 0x0f, 0x2c, 0xcb, 0x8f, 0x4f, 0x96}},
    {0x85f9794b, 0x4d19, 0x40d8, {0x88, 0x64, 0x4e, 0x74, 0x73, 0x71, 0xa6, 0x6d}},
    {0x046b8c80, 0x1647, 0x40f7, {0x9b, 0x21, 0xb9, 0x3b, 0x81, 0xaa, 0xbc, 0x1b}},
    {0xb95f181b, 0xea4c, 0x4af1, {0x80, 0x56, 0x7c, 0x32, 0x1a, 0xbb, 0xb0, 0x91}},
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
    CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                     IID_ITfCategoryMgr, reinterpret_cast<void **>(&mgr));
    HRESULT hr = S_OK;
#if defined(__MINGW32__)
    for (const auto &guid : kRegisterTipCategories) {
#else
    for (const auto &guid : Categories) {
#endif
        hr |= mgr->RegisterCategory(FCITX_CLSID, guid, FCITX_CLSID);
    }
    mgr->Release();
    return hr == S_OK;
}

void UnregisterCategoriesAndProfiles() {
    auto key = "SOFTWARE\\Microsoft\\CTF\\TIP\\" + guidToString(FCITX_CLSID);
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, key.c_str());
}
} // namespace fcitx
