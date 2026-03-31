/*
 * Simple Windows settings UI for the same conf/fcitx5/config + profile files as
 * Linux fcitx5 / fcitx5-config-qt (GlobalConfig + plain profile INI).
 */
#ifndef UNICODE
#define UNICODE 1
#endif
#ifndef _UNICODE
#define _UNICODE 1
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx/globalconfig.h>
#include <fcitx/inputcontextmanager.h>

namespace fcitx {
extern HINSTANCE mainInstanceHandle;
}

namespace {

#pragma comment(lib, "Comctl32.lib")

using fcitx::GlobalConfig;
using fcitx::Key;
using fcitx::KeyStates;
using fcitx::KeyStringFormat;
using fcitx::mainInstanceHandle;
using fcitx::PropertyPropagatePolicy;
using fcitx::PropertyPropagatePolicyToString;
using fcitx::RawConfig;
using fcitx::readAsIni;
using fcitx::safeSaveAsIni;
using fcitx::StandardPaths;
using fcitx::StandardPathsType;

enum CmdId : int {
    IDC_LABEL_HEADER = 1998,
    IDC_LABEL_LANG = 1999,
    IDC_COMBO_LANG = 2000,
    IDC_LABEL_PAGE = 2001,
    IDC_EDIT_PAGE_BUDDY,
    IDC_SPIN_PAGE,
    IDC_CB_ACTIVE,
    IDC_CB_PREEDIT,
    IDC_CB_IM_INFO,
    IDC_CB_ENUM_TRIG,
    IDC_CB_ENUM_SKIP,
    IDC_LABEL_RESET_FOCUS,
    IDC_COMBO_RESET_FOCUS,
    IDC_CB_PWD_IM,
    IDC_CB_PWD_PREEDIT,
    IDC_LABEL_TRIG,
    IDC_EDIT_TRIG,
    IDC_LABEL_ALT,
    IDC_EDIT_ALT,
    IDC_LABEL_ENUMF,
    IDC_EDIT_ENUMF,
    IDC_LABEL_ENUMG,
    IDC_EDIT_ENUMG,
    IDC_LABEL_ENUMB,
    IDC_EDIT_ENUMB,
    IDC_LABEL_ENUMGB,
    IDC_EDIT_ENUMGB,
    IDC_BTN_REC_TRIG = 2200,
    IDC_BTN_REC_ALT,
    IDC_BTN_REC_ENUMF,
    IDC_BTN_REC_ENUMG,
    IDC_BTN_REC_ENUMB,
    IDC_BTN_REC_ENUMGB,
    IDC_GROUP_PROF,
    IDC_EDIT_PROFILE,
    IDC_BTN_SAVE,
    IDC_BTN_RELOAD = 2100,
    IDC_BTN_FOLDER,
    IDC_GROUP_PINYIN,
    IDC_LABEL_SHUANGPIN,
    IDC_COMBO_SHUANGPIN,
    IDC_BTN_APPLY_POPULAR_PROFILE,
    IDC_LABEL_PROFILE_HINT,
};

enum class UiLang {
    ZhCN,
    English,
};

UiLang g_uiLang = UiLang::ZhCN;

enum class TextId {
    WindowTitle,
    Header,
    Language,
    LangChinese,
    LangEnglish,
    CandidatePageSize,
    ActiveByDefault,
    ShowPreeditInApplication,
    ShowInputMethodInformation,
    ResetStateOnFocusIn,
    ResetAll,
    ResetProgram,
    ResetNo,
    AllowInputMethodForPassword,
    ShowPreeditForPassword,
    EnumerateWithTriggerKeys,
    EnumerateSkipFirst,
    TriggerKeys,
    AltTriggerKeys,
    EnumerateForwardKeys,
    EnumerateGroupForward,
    EnumerateBackwardKeys,
    EnumerateGroupBackward,
    PinyinConfig,
    ShuangpinScheme,
    ProfileRawIni,
    ProfileEditHint,
    ApplyRecommendedProfile,
    SaveAll,
    ReloadFromDisk,
    OpenConfigFolder,
    Record,
    SaveConfigFailed,
    SavePinyinFailed,
    SaveProfileFailed,
    SaveSuccess,
    PageSizeInvalid,
    LoadConfigWarning,
    MessageBoxCaption,
    KeyCaptureTitle,
    KeyCaptureHint,
    KeyCaptureCurrent,
    KeyCaptureQueued,
    KeyCaptureEmpty,
    KeyCaptureAppend,
    KeyCaptureDone,
    KeyCaptureCancel,
    KeyCaptureHookFailed,
    KeyCaptureRegisterFailed,
};

const wchar_t *tr(TextId id, UiLang lang) {
    switch (id) {
    case TextId::WindowTitle:
        return lang == UiLang::ZhCN ? L"Fcitx5 设置" : L"Fcitx5 Settings";
    case TextId::Header:
        return lang == UiLang::ZhCN ? L"全局配置（conf/fcitx5/config）- 与 "
                                      L"Linux fcitx5 使用同一套配置"
                                    : L"Global (conf/fcitx5/config) - same "
                                      L"schema as Linux fcitx5";
    case TextId::Language:
        return lang == UiLang::ZhCN ? L"语言：" : L"Language:";
    case TextId::LangChinese:
        return lang == UiLang::ZhCN ? L"中文" : L"Chinese";
    case TextId::LangEnglish:
        return L"English";
    case TextId::CandidatePageSize:
        return lang == UiLang::ZhCN ? L"候选词每页数量（1-10）："
                                    : L"Candidate page size (1-10):";
    case TextId::ActiveByDefault:
        return lang == UiLang::ZhCN ? L"默认启用输入法" : L"Active by default";
    case TextId::ShowPreeditInApplication:
        return lang == UiLang::ZhCN ? L"在应用中显示预编辑文本"
                                    : L"Show preedit in application";
    case TextId::ShowInputMethodInformation:
        return lang == UiLang::ZhCN
                   ? L"切换时显示输入法信息"
                   : L"Show input method information when switching";
    case TextId::ResetStateOnFocusIn:
        return lang == UiLang::ZhCN
                   ? L"获得焦点时重置状态（全部 / 程序 / 不重置）："
                   : L"Reset state on focus in (All / Program / No):";
    case TextId::ResetAll:
        return lang == UiLang::ZhCN ? L"全部" : L"All";
    case TextId::ResetProgram:
        return lang == UiLang::ZhCN ? L"程序" : L"Program";
    case TextId::ResetNo:
        return lang == UiLang::ZhCN ? L"不重置" : L"No";
    case TextId::AllowInputMethodForPassword:
        return lang == UiLang::ZhCN ? L"密码框中允许使用输入法"
                                    : L"Allow input method in password field";
    case TextId::ShowPreeditForPassword:
        return lang == UiLang::ZhCN ? L"输入密码时显示预编辑文本"
                                    : L"Show preedit when typing password";
    case TextId::EnumerateWithTriggerKeys:
        return lang == UiLang::ZhCN
                   ? L"重复按触发键时轮换输入法"
                   : L"Enumerate when pressing trigger key repeatedly";
    case TextId::EnumerateSkipFirst:
        return lang == UiLang::ZhCN ? L"轮换时跳过第一个输入法"
                                    : L"Skip first IM while enumerating";
    case TextId::TriggerKeys:
        return lang == UiLang::ZhCN
                   ? L"触发键（fcitx 语法，空格分隔）："
                   : L"Trigger keys (fcitx syntax, space-separated):";
    case TextId::AltTriggerKeys:
        return lang == UiLang::ZhCN
                   ? L"辅助触发键（例如 Shift_L，临时切换）："
                   : L"Alt trigger keys (e.g. Shift_L - temporary switch):";
    case TextId::EnumerateForwardKeys:
        return lang == UiLang::ZhCN
                   ? L"向前轮换按键（留空为默认；fcitx portable 语法）："
                   : L"Enumerate forward keys (empty = default; fcitx portable "
                     L"key syntax):";
    case TextId::EnumerateGroupForward:
        return lang == UiLang::ZhCN
                   ? L"向前轮换分组（例如 Super+space）："
                   : L"Enumerate group forward (e.g. Super+space):";
    case TextId::EnumerateBackwardKeys:
        return lang == UiLang::ZhCN ? L"向后轮换按键："
                                    : L"Enumerate backward keys:";
    case TextId::EnumerateGroupBackward:
        return lang == UiLang::ZhCN ? L"向后轮换分组："
                                    : L"Enumerate group backward:";
    case TextId::PinyinConfig:
        return lang == UiLang::ZhCN ? L"拼音 / 双拼"
                                    : L"Pinyin / Shuangpin";
    case TextId::ShuangpinScheme:
        return lang == UiLang::ZhCN ? L"双拼方案："
                                    : L"Shuangpin scheme:";
    case TextId::ProfileRawIni:
        return lang == UiLang::ZhCN
                   ? L"配置（输入法 / 分组）- 原始 INI"
                   : L"Profile (input methods / groups) - raw INI";
    case TextId::ProfileEditHint:
        return lang == UiLang::ZhCN
                   ? L"其他输入法仍可在下方原始 INI 中继续添加、删除或修改。"
                   : L"You can still add, remove, or edit other input methods in "
                     L"the raw INI below.";
    case TextId::ApplyRecommendedProfile:
        return lang == UiLang::ZhCN ? L"填入热门输入法模板"
                                    : L"Fill popular IM template";
    case TextId::SaveAll:
        return lang == UiLang::ZhCN ? L"全部保存" : L"Save all";
    case TextId::ReloadFromDisk:
        return lang == UiLang::ZhCN ? L"从磁盘重新加载" : L"Reload from disk";
    case TextId::OpenConfigFolder:
        return lang == UiLang::ZhCN ? L"打开配置文件夹" : L"Open config folder";
    case TextId::Record:
        return lang == UiLang::ZhCN ? L"录制" : L"Record";
    case TextId::SaveConfigFailed:
        return lang == UiLang::ZhCN ? L"无法写入 conf/fcitx5/config。"
                                    : L"Could not write conf/fcitx5/config.";
    case TextId::SavePinyinFailed:
        return lang == UiLang::ZhCN ? L"无法写入 conf/pinyin.conf。"
                                    : L"Could not write conf/pinyin.conf.";
    case TextId::SaveProfileFailed:
        return lang == UiLang::ZhCN ? L"无法写入 profile 文件。"
                                    : L"Could not write profile file.";
    case TextId::SaveSuccess:
        return lang == UiLang::ZhCN ? L"已保存。如果配置未生效，请重启正在使用 "
                                      L"TSF 输入法的应用。"
                                    : L"Saved. Restart apps using the TSF IME "
                                      L"if settings do not apply.";
    case TextId::PageSizeInvalid:
        return lang == UiLang::ZhCN ? L"候选词每页数量必须在 1 到 10 之间。"
                                    : L"Page size must be 1-10.";
    case TextId::LoadConfigWarning:
        return lang == UiLang::ZhCN
                   ? L"无法读取 "
                     L"conf/fcitx5/config（首次运行可在保存时自动创建）。"
                   : L"Could not read conf/fcitx5/config (first run creates it "
                     L"on save).";
    case TextId::MessageBoxCaption:
        return lang == UiLang::ZhCN ? L"Fcitx5 设置" : L"Fcitx5 Settings";
    case TextId::KeyCaptureTitle:
        return lang == UiLang::ZhCN ? L"录制快捷键（WH_KEYBOARD_LL）"
                                    : L"Record shortcut (WH_KEYBOARD_LL)";
    case TextId::KeyCaptureHint:
        return lang == UiLang::ZhCN ? L"按下按键；Esc 关闭且不保存。"
                                    : L"Press keys; Esc closes without saving.";
    case TextId::KeyCaptureCurrent:
        return lang == UiLang::ZhCN ? L"（当前按键）" : L"(current key)";
    case TextId::KeyCaptureQueued:
        return lang == UiLang::ZhCN ? L"已加入：" : L"Queued:";
    case TextId::KeyCaptureEmpty:
        return lang == UiLang::ZhCN ? L"（空，按键后点“加入”）"
                                    : L"(empty -- press keys, Append)";
    case TextId::KeyCaptureAppend:
        return lang == UiLang::ZhCN ? L"加入" : L"Append";
    case TextId::KeyCaptureDone:
        return lang == UiLang::ZhCN ? L"完成" : L"Done";
    case TextId::KeyCaptureCancel:
        return lang == UiLang::ZhCN ? L"取消" : L"Cancel";
    case TextId::KeyCaptureHookFailed:
        return lang == UiLang::ZhCN ? L"无法安装键盘钩子。"
                                    : L"Could not install keyboard hook.";
    case TextId::KeyCaptureRegisterFailed:
        return lang == UiLang::ZhCN
                   ? L"注册快捷键录制窗口类失败。"
                   : L"RegisterClass for capture dialog failed.";
    }
    return L"";
}

void pinStdPathsToThisExe() {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&pinStdPathsToThisExe),
                            &mod) ||
        !mod) {
        return;
    }
    mainInstanceHandle = reinterpret_cast<HINSTANCE>(mod);
}

std::wstring utf8ToWide(std::string_view u8) {
    if (u8.empty()) {
        return {};
    }
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u8.data(),
                                static_cast<int>(u8.size()), nullptr, 0);
    if (n <= 0) {
        return {};
    }
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()),
                        w.data(), n);
    return w;
}

std::string wideToUtf8(std::wstring_view w) {
    if (w.empty()) {
        return {};
    }
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(),
                                static_cast<int>(w.size()), nullptr, 0, nullptr,
                                nullptr);
    if (n <= 0) {
        return {};
    }
    std::string u8(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        u8.data(), n, nullptr, nullptr);
    return u8;
}

void setGuiFont(HWND hwnd) {
    SendMessageW(hwnd, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                 TRUE);
}

std::filesystem::path profilePathForUser() {
    auto p =
        StandardPaths::global().locate(StandardPathsType::PkgConfig, "profile");
    if (!p.empty()) {
        return p;
    }
    return StandardPaths::global().userDirectory(StandardPathsType::PkgConfig) /
           "profile";
}

std::string readProfileUtf8() {
    const auto path = profilePathForUser();
    if (!std::filesystem::exists(path)) {
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeProfileUtf8(const std::string &utf8) {
    const auto path = profilePathForUser();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return static_cast<bool>(out);
}

std::filesystem::path appDataRoot() {
    wchar_t appData[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(appData) / L"Fcitx5";
}

void persistSharedTrayPinyinReloadRequest() {
    const auto path = appDataRoot() / L"pending-tray-pinyin-reload.txt";
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out << "1";
}

struct ShuangpinSchemeOption {
    const char *value;
    const wchar_t *zhText;
    const wchar_t *enText;
};

constexpr ShuangpinSchemeOption kShuangpinSchemes[] = {
    {"Ziranma", L"自然码", L"Ziranma"},
    {"MS", L"微软双拼", L"Microsoft Shuangpin"},
    {"Xiaohe", L"小鹤双拼", L"Xiaohe"},
    {"Ziguang", L"紫光双拼", L"Ziguang"},
    {"ABC", L"智能 ABC", L"ABC"},
    {"Zhongwenzhixing", L"中文之星", L"Zhongwenzhixing"},
    {"PinyinJiajia", L"拼音加加", L"PinyinJiajia"},
    {"Custom", L"自定义", L"Custom"},
};

constexpr const char kPinyinConfigPath[] = "conf/pinyin.conf";

std::string defaultPopularProfileUtf8() {
    return "# Recommended default group: pinyin + shuangpin + wubi + rime\r\n"
           "[Groups/0]\r\n"
           "Name=Default\r\n"
           "Default Layout=us\r\n"
           "DefaultIM=pinyin\r\n"
           "\r\n"
           "[Groups/0/Items/0]\r\n"
           "Name=pinyin\r\n"
           "Layout=\r\n"
           "\r\n"
           "[Groups/0/Items/1]\r\n"
           "Name=shuangpin\r\n"
           "Layout=\r\n"
           "\r\n"
           "[Groups/0/Items/2]\r\n"
           "Name=wbx\r\n"
           "Layout=\r\n"
           "\r\n"
           "[Groups/0/Items/3]\r\n"
           "Name=rime\r\n"
           "Layout=\r\n"
           "\r\n"
           "[GroupOrder]\r\n"
           "0=Default\r\n";
}

int shuangpinSchemeIndexFromValue(std::string_view value) {
    for (size_t i = 0; i < sizeof(kShuangpinSchemes) / sizeof(kShuangpinSchemes[0]);
         ++i) {
        if (value == kShuangpinSchemes[i].value) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

std::string shuangpinSchemeValueFromIndex(int index) {
    if (index < 0 || index >=
                         static_cast<int>(sizeof(kShuangpinSchemes) /
                                          sizeof(kShuangpinSchemes[0]))) {
        return kShuangpinSchemes[0].value;
    }
    return kShuangpinSchemes[index].value;
}

const wchar_t *shuangpinSchemeLabel(int index, UiLang lang) {
    if (index < 0 || index >=
                         static_cast<int>(sizeof(kShuangpinSchemes) /
                                          sizeof(kShuangpinSchemes[0]))) {
        index = 0;
    }
    return lang == UiLang::ZhCN ? kShuangpinSchemes[index].zhText
                                : kShuangpinSchemes[index].enText;
}

struct UiState {
    GlobalConfig gc;
    UiLang lang = UiLang::ZhCN;
    HWND labelHeader = nullptr;
    HWND comboLang = nullptr;
    HWND spinPage = nullptr;
    HWND buddyPage = nullptr;
    HWND cbActive = nullptr;
    HWND cbPreedit = nullptr;
    HWND cbImInfo = nullptr;
    HWND comboResetFocus = nullptr;
    HWND cbPwdIm = nullptr;
    HWND cbPwdPreedit = nullptr;
    HWND cbEnumTrig = nullptr;
    HWND cbEnumSkip = nullptr;
    HWND editTrig = nullptr;
    HWND editAlt = nullptr;
    HWND editEnumF = nullptr;
    HWND editEnumG = nullptr;
    HWND editEnumB = nullptr;
    HWND editEnumGroupB = nullptr;
    HWND comboShuangpin = nullptr;
    HWND editProfile = nullptr;
};

void loadGcFromDisk(UiState &s) {
    readAsIni(s.gc.config(), StandardPathsType::PkgConfig,
              std::filesystem::path("config"));
}

void reloadShuangpinCombo(UiState &st) {
    if (!st.comboShuangpin) {
        return;
    }
    const LRESULT currentSel = SendMessageW(st.comboShuangpin, CB_GETCURSEL, 0, 0);
    SendMessageW(st.comboShuangpin, CB_RESETCONTENT, 0, 0);
    for (int i = 0;
         i < static_cast<int>(sizeof(kShuangpinSchemes) /
                              sizeof(kShuangpinSchemes[0]));
         ++i) {
        SendMessageW(st.comboShuangpin, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(shuangpinSchemeLabel(i, st.lang)));
    }
    SendMessageW(st.comboShuangpin, CB_SETCURSEL,
                 currentSel == CB_ERR ? 0 : currentSel, 0);
}

void syncShuangpinSchemeFromDisk(UiState &st) {
    if (!st.comboShuangpin) {
        return;
    }
    RawConfig raw;
    try {
        readAsIni(raw, StandardPathsType::PkgConfig,
                  std::filesystem::path(kPinyinConfigPath));
    } catch (...) {
    }
    const auto *rootValue = raw.valueByPath("ShuangpinProfile");
    const auto *configValue = raw.valueByPath("Config/ShuangpinProfile");
    const auto value = rootValue ? *rootValue
                                 : (configValue ? *configValue
                                                : std::string_view("Ziranma"));
    const int index = shuangpinSchemeIndexFromValue(value);
    SendMessageW(st.comboShuangpin, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
}

void syncControlsFromGc(HWND hwnd, UiState &st) {
    const int page = st.gc.defaultPageSize();
    SendMessageW(st.spinPage, UDM_SETPOS32, 0,
                 static_cast<LPARAM>(std::max(1, std::min(10, page))));
    CheckDlgButton(hwnd, IDC_CB_ACTIVE,
                   st.gc.activeByDefault() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_CB_PREEDIT,
                   st.gc.preeditEnabledByDefault() ? BST_CHECKED
                                                   : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_CB_IM_INFO,
                   st.gc.showInputMethodInformation() ? BST_CHECKED
                                                      : BST_UNCHECKED);
    {
        PropertyPropagatePolicy const pol = st.gc.resetStateWhenFocusIn();
        int idx = 2;
        if (pol == PropertyPropagatePolicy::All) {
            idx = 0;
        } else if (pol == PropertyPropagatePolicy::Program) {
            idx = 1;
        }
        SendMessageW(st.comboResetFocus, CB_SETCURSEL, static_cast<WPARAM>(idx),
                     0);
    }
    CheckDlgButton(hwnd, IDC_CB_PWD_IM,
                   st.gc.allowInputMethodForPassword() ? BST_CHECKED
                                                       : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_CB_PWD_PREEDIT,
                   st.gc.showPreeditForPassword() ? BST_CHECKED
                                                  : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_CB_ENUM_TRIG,
                   st.gc.enumerateWithTriggerKeys() ? BST_CHECKED
                                                    : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_CB_ENUM_SKIP,
                   st.gc.enumerateSkipFirst() ? BST_CHECKED : BST_UNCHECKED);

    SetWindowTextW(st.editTrig,
                   utf8ToWide(Key::keyListToString(st.gc.triggerKeys(),
                                                   KeyStringFormat::Portable))
                       .c_str());
    SetWindowTextW(st.editAlt,
                   utf8ToWide(Key::keyListToString(st.gc.altTriggerKeys(),
                                                   KeyStringFormat::Portable))
                       .c_str());
    SetWindowTextW(st.editEnumF,
                   utf8ToWide(Key::keyListToString(st.gc.enumerateForwardKeys(),
                                                   KeyStringFormat::Portable))
                       .c_str());
    SetWindowTextW(
        st.editEnumG,
        utf8ToWide(Key::keyListToString(st.gc.enumerateGroupForwardKeys(),
                                        KeyStringFormat::Portable))
            .c_str());
    SetWindowTextW(st.editEnumB, utf8ToWide(Key::keyListToString(
                                                st.gc.enumerateBackwardKeys(),
                                                KeyStringFormat::Portable))
                                     .c_str());
    SetWindowTextW(
        st.editEnumGroupB,
        utf8ToWide(Key::keyListToString(st.gc.enumerateGroupBackwardKeys(),
                                        KeyStringFormat::Portable))
            .c_str());

    syncShuangpinSchemeFromDisk(st);
    SetWindowTextW(st.editProfile, utf8ToWide(readProfileUtf8()).c_str());
}

bool saveAll(HWND hwnd, UiState &st) {
    const int pos =
        static_cast<int>(SendMessageW(st.spinPage, UDM_GETPOS32, 0, 0));
    if (pos < 1 || pos > 10) {
        MessageBoxW(hwnd, tr(TextId::PageSizeInvalid, st.lang),
                    tr(TextId::MessageBoxCaption, st.lang), MB_ICONWARNING);
        return false;
    }

    std::wstring buf;
    buf.resize(static_cast<size_t>(GetWindowTextLengthW(st.editTrig) + 1));
    GetWindowTextW(st.editTrig, buf.data(), static_cast<int>(buf.size()));
    buf.resize(wcslen(buf.c_str()));
    const std::string trig = wideToUtf8(buf);

    buf.assign(static_cast<size_t>(GetWindowTextLengthW(st.editAlt) + 1),
               L'\0');
    GetWindowTextW(st.editAlt, buf.data(), static_cast<int>(buf.size()));
    buf.resize(wcslen(buf.c_str()));
    const std::string alt = wideToUtf8(buf);

    buf.assign(static_cast<size_t>(GetWindowTextLengthW(st.editEnumF) + 1),
               L'\0');
    GetWindowTextW(st.editEnumF, buf.data(), static_cast<int>(buf.size()));
    buf.resize(wcslen(buf.c_str()));
    const std::string enF = wideToUtf8(buf);

    buf.assign(static_cast<size_t>(GetWindowTextLengthW(st.editEnumG) + 1),
               L'\0');
    GetWindowTextW(st.editEnumG, buf.data(), static_cast<int>(buf.size()));
    buf.resize(wcslen(buf.c_str()));
    const std::string enG = wideToUtf8(buf);

    buf.assign(static_cast<size_t>(GetWindowTextLengthW(st.editEnumB) + 1),
               L'\0');
    GetWindowTextW(st.editEnumB, buf.data(), static_cast<int>(buf.size()));
    buf.resize(wcslen(buf.c_str()));
    const std::string enB = wideToUtf8(buf);

    buf.assign(static_cast<size_t>(GetWindowTextLengthW(st.editEnumGroupB) + 1),
               L'\0');
    GetWindowTextW(st.editEnumGroupB, buf.data(), static_cast<int>(buf.size()));
    buf.resize(wcslen(buf.c_str()));
    const std::string enGB = wideToUtf8(buf);

    const LRESULT resetSelR =
        SendMessageW(st.comboResetFocus, CB_GETCURSEL, 0, 0);
    PropertyPropagatePolicy resetPol = PropertyPropagatePolicy::No;
    if (resetSelR == 0) {
        resetPol = PropertyPropagatePolicy::All;
    } else if (resetSelR == 1) {
        resetPol = PropertyPropagatePolicy::Program;
    }

    // Normalize key lists via parse (drops invalid tokens like core does).
    const std::string trigNorm = Key::keyListToString(
        Key::keyListFromString(trig), KeyStringFormat::Portable);
    const std::string altNorm = Key::keyListToString(
        Key::keyListFromString(alt), KeyStringFormat::Portable);
    const std::string enFNorm = Key::keyListToString(
        Key::keyListFromString(enF), KeyStringFormat::Portable);
    const std::string enGNorm = Key::keyListToString(
        Key::keyListFromString(enG), KeyStringFormat::Portable);
    const std::string enBNorm = Key::keyListToString(
        Key::keyListFromString(enB), KeyStringFormat::Portable);
    const std::string enGBNorm = Key::keyListToString(
        Key::keyListFromString(enGB), KeyStringFormat::Portable);

    RawConfig raw;
    st.gc.save(raw);
    raw.setValueByPath("Behavior/DefaultPageSize", std::to_string(pos));
    raw.setValueByPath("Behavior/ActiveByDefault",
                       IsDlgButtonChecked(hwnd, IDC_CB_ACTIVE) == BST_CHECKED
                           ? "True"
                           : "False");
    raw.setValueByPath("Behavior/PreeditEnabledByDefault",
                       IsDlgButtonChecked(hwnd, IDC_CB_PREEDIT) == BST_CHECKED
                           ? "True"
                           : "False");
    raw.setValueByPath("Behavior/ShowInputMethodInformation",
                       IsDlgButtonChecked(hwnd, IDC_CB_IM_INFO) == BST_CHECKED
                           ? "True"
                           : "False");
    raw.setValueByPath("Behavior/resetStateWhenFocusIn",
                       PropertyPropagatePolicyToString(resetPol));
    raw.setValueByPath("Behavior/AllowInputMethodForPassword",
                       IsDlgButtonChecked(hwnd, IDC_CB_PWD_IM) == BST_CHECKED
                           ? "True"
                           : "False");
    raw.setValueByPath(
        "Behavior/ShowPreeditForPassword",
        IsDlgButtonChecked(hwnd, IDC_CB_PWD_PREEDIT) == BST_CHECKED ? "True"
                                                                    : "False");
    raw.setValueByPath("Hotkey/EnumerateWithTriggerKeys",
                       IsDlgButtonChecked(hwnd, IDC_CB_ENUM_TRIG) == BST_CHECKED
                           ? "True"
                           : "False");
    raw.setValueByPath("Hotkey/EnumerateSkipFirst",
                       IsDlgButtonChecked(hwnd, IDC_CB_ENUM_SKIP) == BST_CHECKED
                           ? "True"
                           : "False");
    raw.setValueByPath("Hotkey/TriggerKeys", trigNorm);
    raw.setValueByPath("Hotkey/AltTriggerKeys", altNorm);
    raw.setValueByPath("Hotkey/EnumerateForwardKeys", enFNorm);
    raw.setValueByPath("Hotkey/EnumerateGroupForwardKeys", enGNorm);
    raw.setValueByPath("Hotkey/EnumerateBackwardKeys", enBNorm);
    raw.setValueByPath("Hotkey/EnumerateGroupBackwardKeys", enGBNorm);

    st.gc.load(raw);
    if (!safeSaveAsIni(st.gc.config(), StandardPathsType::PkgConfig,
                       std::filesystem::path("config"))) {
        MessageBoxW(hwnd, tr(TextId::SaveConfigFailed, st.lang),
                    tr(TextId::MessageBoxCaption, st.lang), MB_ICONERROR);
        return false;
    }

    {
        RawConfig raw;
        try {
            readAsIni(raw, StandardPathsType::PkgConfig,
                      std::filesystem::path(kPinyinConfigPath));
        } catch (...) {
        }
        const int shuangpinSel = static_cast<int>(
            SendMessageW(st.comboShuangpin, CB_GETCURSEL, 0, 0));
        const auto value = shuangpinSchemeValueFromIndex(shuangpinSel);
        raw.setValueByPath("ShuangpinProfile", value);
        raw.setValueByPath("Config/ShuangpinProfile", value);
        if (!safeSaveAsIni(raw, StandardPathsType::PkgConfig,
                           std::filesystem::path(kPinyinConfigPath))) {
            MessageBoxW(hwnd, tr(TextId::SavePinyinFailed, st.lang),
                        tr(TextId::MessageBoxCaption, st.lang), MB_ICONERROR);
            return false;
        }
        persistSharedTrayPinyinReloadRequest();
    }

    std::wstring prof;
    const int plen = GetWindowTextLengthW(st.editProfile);
    prof.resize(static_cast<size_t>(plen + 1), L'\0');
    GetWindowTextW(st.editProfile, prof.data(), plen + 1);
    prof.resize(wcslen(prof.c_str()));
    if (!writeProfileUtf8(wideToUtf8(prof))) {
        MessageBoxW(hwnd, tr(TextId::SaveProfileFailed, st.lang),
                    tr(TextId::MessageBoxCaption, st.lang), MB_ICONERROR);
        return false;
    }

    MessageBoxW(hwnd, tr(TextId::SaveSuccess, st.lang),
                tr(TextId::MessageBoxCaption, st.lang), MB_ICONINFORMATION);
    return true;
}

HWND label(HWND parent, int id, const wchar_t *text, int x, int y, int w,
           int h) {
    HWND hW = CreateWindowExW(
        0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)),
        nullptr);
    setGuiFont(hW);
    return hW;
}

HWND mkBtn(HWND parent, int id, const wchar_t *text, int x, int y, int w,
           int h) {
    HWND b = CreateWindowExW(
        0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w, h,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)),
        nullptr);
    setGuiFont(b);
    return b;
}

void setControlText(HWND hwnd, int id, const wchar_t *text) {
    if (HWND child = GetDlgItem(hwnd, id)) {
        SetWindowTextW(child, text);
    }
}

void reloadResetFocusCombo(HWND hwnd, UiState &st) {
    const LRESULT currentSel =
        SendMessageW(st.comboResetFocus, CB_GETCURSEL, 0, 0);
    SendMessageW(st.comboResetFocus, CB_RESETCONTENT, 0, 0);
    SendMessageW(st.comboResetFocus, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(tr(TextId::ResetAll, st.lang)));
    SendMessageW(st.comboResetFocus, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(tr(TextId::ResetProgram, st.lang)));
    SendMessageW(st.comboResetFocus, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(tr(TextId::ResetNo, st.lang)));
    SendMessageW(st.comboResetFocus, CB_SETCURSEL,
                 currentSel == CB_ERR ? 2 : currentSel, 0);
}

void reloadLanguageCombo(UiState &st) {
    if (!st.comboLang) {
        return;
    }
    const LRESULT currentSel = SendMessageW(st.comboLang, CB_GETCURSEL, 0, 0);
    SendMessageW(st.comboLang, CB_RESETCONTENT, 0, 0);
    SendMessageW(st.comboLang, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(tr(TextId::LangChinese, st.lang)));
    SendMessageW(st.comboLang, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(tr(TextId::LangEnglish, st.lang)));
    SendMessageW(st.comboLang, CB_SETCURSEL,
                 currentSel == CB_ERR ? (st.lang == UiLang::ZhCN ? 0 : 1)
                                      : currentSel,
                 0);
}

void applyLanguage(HWND hwnd, UiState &st) {
    g_uiLang = st.lang;
    SetWindowTextW(hwnd, tr(TextId::WindowTitle, st.lang));
    if (st.labelHeader) {
        SetWindowTextW(st.labelHeader, tr(TextId::Header, st.lang));
    }
    reloadLanguageCombo(st);
    setControlText(hwnd, IDC_LABEL_LANG, tr(TextId::Language, st.lang));
    setControlText(hwnd, IDC_LABEL_PAGE,
                   tr(TextId::CandidatePageSize, st.lang));
    setControlText(hwnd, IDC_CB_ACTIVE, tr(TextId::ActiveByDefault, st.lang));
    setControlText(hwnd, IDC_CB_PREEDIT,
                   tr(TextId::ShowPreeditInApplication, st.lang));
    setControlText(hwnd, IDC_CB_IM_INFO,
                   tr(TextId::ShowInputMethodInformation, st.lang));
    setControlText(hwnd, IDC_LABEL_RESET_FOCUS,
                   tr(TextId::ResetStateOnFocusIn, st.lang));
    reloadResetFocusCombo(hwnd, st);
    setControlText(hwnd, IDC_CB_PWD_IM,
                   tr(TextId::AllowInputMethodForPassword, st.lang));
    setControlText(hwnd, IDC_CB_PWD_PREEDIT,
                   tr(TextId::ShowPreeditForPassword, st.lang));
    setControlText(hwnd, IDC_CB_ENUM_TRIG,
                   tr(TextId::EnumerateWithTriggerKeys, st.lang));
    setControlText(hwnd, IDC_CB_ENUM_SKIP,
                   tr(TextId::EnumerateSkipFirst, st.lang));
    setControlText(hwnd, IDC_LABEL_TRIG, tr(TextId::TriggerKeys, st.lang));
    setControlText(hwnd, IDC_LABEL_ALT, tr(TextId::AltTriggerKeys, st.lang));
    setControlText(hwnd, IDC_LABEL_ENUMF,
                   tr(TextId::EnumerateForwardKeys, st.lang));
    setControlText(hwnd, IDC_LABEL_ENUMG,
                   tr(TextId::EnumerateGroupForward, st.lang));
    setControlText(hwnd, IDC_LABEL_ENUMB,
                   tr(TextId::EnumerateBackwardKeys, st.lang));
    setControlText(hwnd, IDC_LABEL_ENUMGB,
                   tr(TextId::EnumerateGroupBackward, st.lang));
    setControlText(hwnd, IDC_GROUP_PINYIN, tr(TextId::PinyinConfig, st.lang));
    setControlText(hwnd, IDC_LABEL_SHUANGPIN,
                   tr(TextId::ShuangpinScheme, st.lang));
    reloadShuangpinCombo(st);
    syncShuangpinSchemeFromDisk(st);
    setControlText(hwnd, IDC_GROUP_PROF, tr(TextId::ProfileRawIni, st.lang));
    setControlText(hwnd, IDC_LABEL_PROFILE_HINT,
                   tr(TextId::ProfileEditHint, st.lang));
    setControlText(hwnd, IDC_BTN_APPLY_POPULAR_PROFILE,
                   tr(TextId::ApplyRecommendedProfile, st.lang));
    setControlText(hwnd, IDC_BTN_SAVE, tr(TextId::SaveAll, st.lang));
    setControlText(hwnd, IDC_BTN_RELOAD, tr(TextId::ReloadFromDisk, st.lang));
    setControlText(hwnd, IDC_BTN_FOLDER, tr(TextId::OpenConfigFolder, st.lang));
    setControlText(hwnd, IDC_BTN_REC_TRIG, tr(TextId::Record, st.lang));
    setControlText(hwnd, IDC_BTN_REC_ALT, tr(TextId::Record, st.lang));
    setControlText(hwnd, IDC_BTN_REC_ENUMF, tr(TextId::Record, st.lang));
    setControlText(hwnd, IDC_BTN_REC_ENUMG, tr(TextId::Record, st.lang));
    setControlText(hwnd, IDC_BTN_REC_ENUMB, tr(TextId::Record, st.lang));
    setControlText(hwnd, IDC_BTN_REC_ENUMGB, tr(TextId::Record, st.lang));
}

namespace keycap {

KeyStates winModifierStates() {
    KeyStates s;
    if (GetKeyState(VK_SHIFT) < 0) {
        s |= fcitx::KeyState::Shift;
    }
    if (GetKeyState(VK_CONTROL) < 0) {
        s |= fcitx::KeyState::Ctrl;
    }
    if (GetKeyState(VK_MENU) < 0) {
        s |= fcitx::KeyState::Alt;
    }
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) {
        s |= fcitx::KeyState::Super;
    }
    return s;
}

Key keyFromWindowsVk(unsigned vk, LPARAM lParam) {
    const LPARAM lp = static_cast<LPARAM>(lParam);
    const bool ext = (lp & (1 << 24)) != 0;
    const KeyStates st = winModifierStates();
    switch (vk) {
    case VK_LEFT:
        return Key(FcitxKey_Left, st);
    case VK_RIGHT:
        return Key(FcitxKey_Right, st);
    case VK_UP:
        return Key(FcitxKey_Up, st);
    case VK_DOWN:
        return Key(FcitxKey_Down, st);
    case VK_RETURN:
        return ext ? Key(FcitxKey_KP_Enter, st) : Key(FcitxKey_Return, st);
    case VK_TAB:
        return Key(FcitxKey_Tab, st);
    case VK_SPACE:
        return Key(FcitxKey_space, st);
    case VK_BACK:
        return Key(FcitxKey_BackSpace, st);
    case VK_ESCAPE:
        return Key(FcitxKey_Escape, st);
    case VK_LSHIFT:
        return Key(FcitxKey_Shift_L, st);
    case VK_RSHIFT:
        return Key(FcitxKey_Shift_R, st);
    case VK_LCONTROL:
        return Key(FcitxKey_Control_L, st);
    case VK_RCONTROL:
        return Key(FcitxKey_Control_R, st);
    case VK_LMENU:
        return Key(FcitxKey_Alt_L, st);
    case VK_RMENU:
        return Key(FcitxKey_Alt_R, st);
    case VK_LWIN:
        return Key(FcitxKey_Super_L, st);
    case VK_RWIN:
        return Key(FcitxKey_Super_R, st);
    case VK_PRIOR:
        return Key(FcitxKey_Page_Up, st);
    case VK_NEXT:
        return Key(FcitxKey_Page_Down, st);
    case VK_HOME:
        return Key(FcitxKey_Home, st);
    case VK_END:
        return Key(FcitxKey_End, st);
    case VK_DELETE:
        return Key(FcitxKey_Delete, st);
    default:
        break;
    }
    if (vk >= static_cast<unsigned>('0') && vk <= static_cast<unsigned>('9')) {
        return Key(static_cast<fcitx::KeySym>(FcitxKey_0 + (vk - '0')), st);
    }
    if (vk >= static_cast<unsigned>('A') && vk <= static_cast<unsigned>('Z')) {
        wchar_t c = static_cast<wchar_t>(vk);
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
        if (!(caps ^ shift)) {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
        return Key(static_cast<fcitx::KeySym>(FcitxKey_a + (c - L'a')), st);
    }
    return Key::fromKeyCode(static_cast<int>(vk), st);
}

struct KeyCaptureUi {
    HWND dlgHwnd = nullptr;
    HWND targetEdit = nullptr;
    HWND stPreview = nullptr;
    HWND stAccum = nullptr;
    std::string accumulated;
    bool haveLast = false;
    Key lastKey = Key();
    HHOOK hook = nullptr;
};

KeyCaptureUi *g_capUi = nullptr;

LRESULT CALLBACK KeyCapHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION || !g_capUi || !g_capUi->stPreview) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }
    if (wParam != WM_KEYDOWN && wParam != WM_SYSKEYDOWN) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }
    const auto *pk = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
    if (pk->flags & LLKHF_UP) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }
    const HWND fg = GetForegroundWindow();
    if (fg == g_capUi->dlgHwnd || IsChild(g_capUi->dlgHwnd, GetFocus())) {
        if (pk->vkCode == VK_TAB || pk->vkCode == VK_LEFT ||
            pk->vkCode == VK_RIGHT || pk->vkCode == VK_UP ||
            pk->vkCode == VK_DOWN) {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }
    }
    const LPARAM synLparam = (pk->flags & LLKHF_EXTENDED) ? (1 << 24) : 0;
    Key const k = keyFromWindowsVk(pk->vkCode, synLparam);
    if (pk->vkCode == VK_ESCAPE) {
        PostMessageW(g_capUi->dlgHwnd, WM_CLOSE, 0, 0);
        return 1;
    }
    g_capUi->haveLast = true;
    g_capUi->lastKey = k;
    const std::string s = k.toString(KeyStringFormat::Portable);
    SetWindowTextW(g_capUi->stPreview, utf8ToWide(s).c_str());
    return 1;
}

enum KeyCapCmd : int {
    ID_CAP_APPEND = 3101,
    ID_CAP_DONE = 3102,
    ID_CAP_CANCEL = 3103,
};

void appendLastChord(KeyCaptureUi *ctx) {
    if (!ctx->haveLast) {
        return;
    }
    const std::string piece = ctx->lastKey.toString(KeyStringFormat::Portable);
    if (!piece.empty()) {
        if (!ctx->accumulated.empty()) {
            ctx->accumulated.push_back(' ');
        }
        ctx->accumulated += piece;
    }
    SetWindowTextW(
        ctx->stAccum,
        utf8ToWide(ctx->accumulated.empty()
                       ? wideToUtf8(tr(TextId::KeyCaptureEmpty, g_uiLang))
                       : ctx->accumulated)
            .c_str());
}

void finishCapture(KeyCaptureUi *ctx, bool apply) {
    if (!apply) {
        DestroyWindow(ctx->dlgHwnd);
        return;
    }
    std::wstring cur;
    const int n = GetWindowTextLengthW(ctx->targetEdit);
    if (n > 0) {
        cur.resize(static_cast<size_t>(n) + 1);
        GetWindowTextW(ctx->targetEdit, cur.data(), n + 1);
        cur.resize(wcslen(cur.c_str()));
    }
    std::wstring const nw = utf8ToWide(ctx->accumulated);
    if (!cur.empty() && !nw.empty()) {
        cur.push_back(L' ');
    }
    cur += nw;
    SetWindowTextW(ctx->targetEdit, cur.c_str());
    DestroyWindow(ctx->dlgHwnd);
}

LRESULT CALLBACK KeyCapDlgProc(HWND hwnd, UINT msg, WPARAM wParam,
                               LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
        auto *ctx = reinterpret_cast<KeyCaptureUi *>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
        ctx->dlgHwnd = hwnd;
        g_capUi = ctx;
        const HINSTANCE inst = cs->hInstance;
        label(hwnd, 0, tr(TextId::KeyCaptureHint, g_uiLang), 12, 12, 440, 18);
        ctx->stPreview = label(hwnd, 0, tr(TextId::KeyCaptureCurrent, g_uiLang),
                               12, 34, 440, 20);
        label(hwnd, 0, tr(TextId::KeyCaptureQueued, g_uiLang), 12, 58, 120, 18);
        ctx->stAccum = label(hwnd, 0, tr(TextId::KeyCaptureEmpty, g_uiLang), 12,
                             78, 440, 20);
        mkBtn(hwnd, ID_CAP_APPEND, tr(TextId::KeyCaptureAppend, g_uiLang), 12,
              108, 120, 26);
        mkBtn(hwnd, ID_CAP_DONE, tr(TextId::KeyCaptureDone, g_uiLang), 140, 108,
              120, 26);
        mkBtn(hwnd, ID_CAP_CANCEL, tr(TextId::KeyCaptureCancel, g_uiLang), 268,
              108, 100, 26);
        ctx->hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyCapHookProc,
                                      GetModuleHandleW(nullptr), 0);
        if (!ctx->hook) {
            MessageBoxW(hwnd, tr(TextId::KeyCaptureHookFailed, g_uiLang),
                        tr(TextId::MessageBoxCaption, g_uiLang),
                        MB_ICONWARNING);
        }
        (void)inst;
        return 0;
    }
    case WM_COMMAND: {
        auto *ctx = reinterpret_cast<KeyCaptureUi *>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!ctx) {
            break;
        }
        switch (LOWORD(wParam)) {
        case ID_CAP_APPEND:
            appendLastChord(ctx);
            return 0;
        case ID_CAP_DONE:
            finishCapture(ctx, true);
            return 0;
        case ID_CAP_CANCEL:
            finishCapture(ctx, false);
            return 0;
        default:
            break;
        }
        break;
    }
    case WM_DESTROY: {
        auto *ctx = reinterpret_cast<KeyCaptureUi *>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (ctx) {
            if (ctx->hook) {
                UnhookWindowsHookEx(ctx->hook);
                ctx->hook = nullptr;
            }
            g_capUi = nullptr;
            delete ctx;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void runKeyCaptureModal(HWND owner, HWND targetEdit, HINSTANCE inst) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = KeyCapDlgProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"Fcitx5KeyCapCls";
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassW(&wc) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            MessageBoxW(owner, tr(TextId::KeyCaptureRegisterFailed, g_uiLang),
                        tr(TextId::MessageBoxCaption, g_uiLang), MB_ICONERROR);
            return;
        }
        registered = true;
    }
    auto *ctx = new KeyCaptureUi{};
    ctx->targetEdit = targetEdit;
    EnableWindow(owner, FALSE);
    RECT rr{};
    GetWindowRect(owner, &rr);
    const int ww = 468;
    const int hh = 175;
    const int x = rr.left + (rr.right - rr.left - ww) / 2;
    const int y = rr.top + (rr.bottom - rr.top - hh) / 2;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
                               L"Fcitx5KeyCapCls",
                               tr(TextId::KeyCaptureTitle, g_uiLang),
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, ww, hh,
                               owner, nullptr, inst, ctx);
    if (!dlg) {
        delete ctx;
        EnableWindow(owner, TRUE);
        return;
    }
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    MSG msg{};
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

} // namespace keycap

void createUi(HWND hwnd, UiState &st, HINSTANCE inst) {
    int y = 12;
    st.labelHeader = label(hwnd, IDC_LABEL_HEADER, tr(TextId::Header, st.lang),
                           12, y, 360, 18);
    label(hwnd, IDC_LABEL_LANG, tr(TextId::Language, st.lang), 384, y + 2, 52,
          18);
    st.comboLang = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 438, y - 2, 74,
        120, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_LANG)), inst,
        nullptr);
    setGuiFont(st.comboLang);
    SendMessageW(st.comboLang, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(tr(TextId::LangChinese, st.lang)));
    SendMessageW(st.comboLang, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(tr(TextId::LangEnglish, st.lang)));
    SendMessageW(st.comboLang, CB_SETCURSEL, st.lang == UiLang::ZhCN ? 0 : 1,
                 0);
    y += 28;
    label(hwnd, IDC_LABEL_PAGE, tr(TextId::CandidatePageSize, st.lang), 12,
          y + 2, 200, 20);
    st.buddyPage = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"5",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT, 220, y, 48, 22, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_PAGE_BUDDY)),
        inst, nullptr);
    setGuiFont(st.buddyPage);
    st.spinPage = CreateWindowExW(
        0, UPDOWN_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT |
            UDS_ARROWKEYS | UDS_NOTHOUSANDS,
        268, y, 60, 22, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPIN_PAGE)), inst,
        nullptr);
    SendMessageW(st.spinPage, UDM_SETBUDDY,
                 reinterpret_cast<WPARAM>(st.buddyPage), 0);
    SendMessageW(st.spinPage, UDM_SETRANGE32, 1, 10);
    y += 32;

#define CB(var, id, txt)                                                       \
    st.var = CreateWindowExW(                                                  \
        0, L"BUTTON", txt, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 12, y,     \
        480, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),      \
        inst, nullptr);                                                        \
    setGuiFont(st.var);                                                        \
    y += 26

    CB(cbActive, IDC_CB_ACTIVE, tr(TextId::ActiveByDefault, st.lang));
    CB(cbPreedit, IDC_CB_PREEDIT,
       tr(TextId::ShowPreeditInApplication, st.lang));
    CB(cbImInfo, IDC_CB_IM_INFO,
       tr(TextId::ShowInputMethodInformation, st.lang));

    label(hwnd, IDC_LABEL_RESET_FOCUS, tr(TextId::ResetStateOnFocusIn, st.lang),
          12, y + 2, 270, 20);
    st.comboResetFocus = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 288, y, 200, 160,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_RESET_FOCUS)),
        inst, nullptr);
    setGuiFont(st.comboResetFocus);
    SendMessageW(st.comboResetFocus, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(tr(TextId::ResetAll, st.lang)));
    SendMessageW(st.comboResetFocus, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(tr(TextId::ResetProgram, st.lang)));
    SendMessageW(st.comboResetFocus, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(tr(TextId::ResetNo, st.lang)));
    y += 30;

    CB(cbPwdIm, IDC_CB_PWD_IM,
       tr(TextId::AllowInputMethodForPassword, st.lang));
    CB(cbPwdPreedit, IDC_CB_PWD_PREEDIT,
       tr(TextId::ShowPreeditForPassword, st.lang));

    CB(cbEnumTrig, IDC_CB_ENUM_TRIG,
       tr(TextId::EnumerateWithTriggerKeys, st.lang));
    CB(cbEnumSkip, IDC_CB_ENUM_SKIP, tr(TextId::EnumerateSkipFirst, st.lang));

    label(hwnd, IDC_LABEL_TRIG, tr(TextId::TriggerKeys, st.lang), 12, y, 500,
          18);
    y += 20;
    st.editTrig = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        12, y, 400, 24, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_TRIG)), inst,
        nullptr);
    setGuiFont(st.editTrig);
    mkBtn(hwnd, IDC_BTN_REC_TRIG, tr(TextId::Record, st.lang), 418, y - 1, 94,
          26);
    y += 30;
    label(hwnd, IDC_LABEL_ALT, tr(TextId::AltTriggerKeys, st.lang), 12, y, 500,
          18);
    y += 20;
    st.editAlt = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        12, y, 400, 24, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_ALT)), inst,
        nullptr);
    setGuiFont(st.editAlt);
    mkBtn(hwnd, IDC_BTN_REC_ALT, tr(TextId::Record, st.lang), 418, y - 1, 94,
          26);
    y += 30;
    label(hwnd, IDC_LABEL_ENUMF, tr(TextId::EnumerateForwardKeys, st.lang), 12,
          y, 500, 18);
    y += 20;
    st.editEnumF = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        12, y, 400, 24, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_ENUMF)), inst,
        nullptr);
    setGuiFont(st.editEnumF);
    mkBtn(hwnd, IDC_BTN_REC_ENUMF, tr(TextId::Record, st.lang), 418, y - 1, 94,
          26);
    y += 30;
    label(hwnd, IDC_LABEL_ENUMG, tr(TextId::EnumerateGroupForward, st.lang), 12,
          y, 500, 18);
    y += 20;
    st.editEnumG = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        12, y, 400, 24, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_ENUMG)), inst,
        nullptr);
    setGuiFont(st.editEnumG);
    mkBtn(hwnd, IDC_BTN_REC_ENUMG, tr(TextId::Record, st.lang), 418, y - 1, 94,
          26);
    y += 30;
    label(hwnd, IDC_LABEL_ENUMB, tr(TextId::EnumerateBackwardKeys, st.lang), 12,
          y, 500, 18);
    y += 20;
    st.editEnumB = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        12, y, 400, 24, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_ENUMB)), inst,
        nullptr);
    setGuiFont(st.editEnumB);
    mkBtn(hwnd, IDC_BTN_REC_ENUMB, tr(TextId::Record, st.lang), 418, y - 1, 94,
          26);
    y += 30;
    label(hwnd, IDC_LABEL_ENUMGB, tr(TextId::EnumerateGroupBackward, st.lang),
          12, y, 500, 18);
    y += 20;
    st.editEnumGroupB = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        12, y, 400, 24, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_ENUMGB)), inst,
        nullptr);
    setGuiFont(st.editEnumGroupB);
    mkBtn(hwnd, IDC_BTN_REC_ENUMGB, tr(TextId::Record, st.lang), 418, y - 1, 94,
          26);
    y += 28;

    label(hwnd, IDC_GROUP_PINYIN, tr(TextId::PinyinConfig, st.lang), 12, y, 500,
          18);
    y += 20;
    label(hwnd, IDC_LABEL_SHUANGPIN, tr(TextId::ShuangpinScheme, st.lang), 12,
          y + 3, 150, 20);
    st.comboShuangpin = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 168, y, 170, 180,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_SHUANGPIN)), inst,
        nullptr);
    setGuiFont(st.comboShuangpin);
    reloadShuangpinCombo(st);
    mkBtn(hwnd, IDC_BTN_APPLY_POPULAR_PROFILE,
          tr(TextId::ApplyRecommendedProfile, st.lang), 346, y - 1, 166, 26);
    y += 32;
    label(hwnd, IDC_LABEL_PROFILE_HINT, tr(TextId::ProfileEditHint, st.lang), 12,
          y, 500, 18);
    y += 24;

    label(hwnd, IDC_GROUP_PROF, tr(TextId::ProfileRawIni, st.lang), 12, y, 500,
          18);
    y += 20;
    st.editProfile = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_WANTRETURN,
        12, y, 500, 180, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_PROFILE)), inst,
        nullptr);
    setGuiFont(st.editProfile);
    y += 188;

    mkBtn(hwnd, IDC_BTN_SAVE, tr(TextId::SaveAll, st.lang), 12, y, 120, 28);
    mkBtn(hwnd, IDC_BTN_RELOAD, tr(TextId::ReloadFromDisk, st.lang), 140, y,
          140, 28);
    mkBtn(hwnd, IDC_BTN_FOLDER, tr(TextId::OpenConfigFolder, st.lang), 290, y,
          160, 28);
#undef CB
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto *st =
        reinterpret_cast<UiState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        pinStdPathsToThisExe();
        INITCOMMONCONTROLSEX icc{};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_STANDARD_CLASSES | ICC_UPDOWN_CLASS;
        InitCommonControlsEx(&icc);

        auto *state = new UiState{};
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(state));
        try {
            loadGcFromDisk(*state);
        } catch (...) {
            MessageBoxW(hwnd, tr(TextId::LoadConfigWarning, state->lang),
                        tr(TextId::MessageBoxCaption, state->lang),
                        MB_ICONWARNING);
        }
        const auto createInst =
            reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance;
        createUi(hwnd, *state, createInst);
        applyLanguage(hwnd, *state);
        syncControlsFromGc(hwnd, *state);
        return 0;
    }
    case WM_COMMAND: {
        if (!st) {
            break;
        }
        const int id = LOWORD(wParam);
        if (id == IDC_COMBO_LANG && HIWORD(wParam) == CBN_SELCHANGE &&
            st->comboLang) {
            const LRESULT sel = SendMessageW(st->comboLang, CB_GETCURSEL, 0, 0);
            st->lang = sel == 1 ? UiLang::English : UiLang::ZhCN;
            applyLanguage(hwnd, *st);
            return 0;
        }
        if (id == IDC_BTN_SAVE) {
            saveAll(hwnd, *st);
            return 0;
        }
        if (id == IDC_BTN_RELOAD) {
            try {
                loadGcFromDisk(*st);
            } catch (...) {
            }
            syncControlsFromGc(hwnd, *st);
            return 0;
        }
        if (id == IDC_BTN_FOLDER) {
            const auto dir = StandardPaths::global().userDirectory(
                StandardPathsType::PkgConfig);
            if (!dir.empty()) {
                ShellExecuteW(hwnd, L"explore", dir.wstring().c_str(), nullptr,
                              nullptr, SW_SHOWNORMAL);
            }
            return 0;
        }
        if (id == IDC_BTN_APPLY_POPULAR_PROFILE) {
            SetWindowTextW(st->editProfile,
                           utf8ToWide(defaultPopularProfileUtf8()).c_str());
            return 0;
        }
        const HINSTANCE inst = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        if (id == IDC_BTN_REC_TRIG) {
            keycap::runKeyCaptureModal(hwnd, st->editTrig, inst);
            return 0;
        }
        if (id == IDC_BTN_REC_ALT) {
            keycap::runKeyCaptureModal(hwnd, st->editAlt, inst);
            return 0;
        }
        if (id == IDC_BTN_REC_ENUMF) {
            keycap::runKeyCaptureModal(hwnd, st->editEnumF, inst);
            return 0;
        }
        if (id == IDC_BTN_REC_ENUMG) {
            keycap::runKeyCaptureModal(hwnd, st->editEnumG, inst);
            return 0;
        }
        if (id == IDC_BTN_REC_ENUMB) {
            keycap::runKeyCaptureModal(hwnd, st->editEnumB, inst);
            return 0;
        }
        if (id == IDC_BTN_REC_ENUMGB) {
            keycap::runKeyCaptureModal(hwnd, st->editEnumGroupB, inst);
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        delete st;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    const wchar_t *cls = L"Fcitx5ConfigWin32Cls";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = cls;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW, cls, tr(TextId::WindowTitle, g_uiLang),
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, CW_USEDEFAULT,
        CW_USEDEFAULT, 560, 980, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
