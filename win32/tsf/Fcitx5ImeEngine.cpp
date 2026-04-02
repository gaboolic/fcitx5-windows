#include "Fcitx5ImeEngine.h"
#include "Fcitx5ImePipeShared.h"
#include "TsfInputContext.h"
#include "TsfTrace.h"

#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/environ.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx/action.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/globalconfig.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterfacemanager.h>

#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx/event.h>

#include <uv.h>

#include <Windows.h>
#include <winreg.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>

namespace fcitx {

Instance *Fcitx5ImeEngine::instancePtr() const {
    if (pipeSharedHost_) {
        return pipeSharedHost_->instance();
    }
    return instance_.get();
}

// Fcitx5Utils (standardpaths_p_win.cpp): pin portable layout to the IME DLL,
// not the host process .exe (Notepad, etc.).
extern HINSTANCE mainInstanceHandle;

namespace ime_engine_file {

// Fcitx5.exe runs eventLoop().exec(); the TSF DLL never does, so libuv async
// (used by EventDispatcher for UI/input-panel updates) would never run. Pump a
// few non-blocking iterations before reading InputPanel in syncUiFromIc().
void flushLibuvLoopForIme(EventLoop &loop) {
    if (std::strcmp(loop.implementation(), "libuv") != 0) {
        return;
    }
    void *native = loop.nativeHandle();
    if (!native) {
        return;
    }
    auto *uvloop = static_cast<uv_loop_t *>(native);
    for (int i = 0; i < 24; ++i) {
        uv_run(uvloop, UV_RUN_NOWAIT);
    }
}

int utf8PrefixToWideLen(const std::string &utf8, int byteCount) {
    if (byteCount <= 0 || utf8.empty()) {
        return 0;
    }
    const int nBytes =
        std::min(byteCount, static_cast<int>(static_cast<int>(utf8.size())));
    const int w =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), nBytes, nullptr, 0);
    return w > 0 ? w : 0;
}

std::wstring utf8ToWide(const std::string &utf8) {
    if (utf8.empty()) {
        return {};
    }
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                static_cast<int>(utf8.size()), nullptr, 0);
    // Invalid UTF-8 sequences (e.g. rare addon output) would make strict
    // conversion fail and leave inline preedit empty; fall back to lenient.
    if (n <= 0) {
        n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                static_cast<int>(utf8.size()), nullptr, 0);
    }
    if (n <= 0) {
        return {};
    }
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        w.data(), n);
    return w;
}

std::wstring asciiLowerWide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) -> wchar_t {
                       if (ch >= L'A' && ch <= L'Z') {
                           return static_cast<wchar_t>(ch - L'A' + L'a');
                       }
                       return ch;
                   });
    return value;
}

Action *lookupTrayStatusAction(Instance *instance, InputContext *ic,
                               const char *name) {
    if (!instance || !ic || !name || !*name) {
        return nullptr;
    }
    instance->addonManager().addon(name, true);
    auto *action = instance->userInterfaceManager().lookupAction(name);
    if (!action) {
        return nullptr;
    }
    if (!action->isParent(&ic->statusArea())) {
        ic->statusArea().addAction(StatusGroup::AfterInputMethod, action);
    }
    return action;
}

bool trayStatusActionChecked(std::string_view actionName,
                             const std::wstring &shortText) {
    const auto lower = asciiLowerWide(shortText);
    if (actionName == "punctuation") {
        return lower.find(L"full width") != std::wstring::npos ||
               shortText.find(L"\x5168\x89d2") != std::wstring::npos;
    }
    if (actionName == "fullwidth") {
        return lower.find(L"full width") != std::wstring::npos ||
               shortText.find(L"\x5168\x89d2") != std::wstring::npos;
    }
    if (actionName == "chttrans") {
        return lower.find(L"traditional") != std::wstring::npos ||
               shortText.find(L"\x7e41\x4f53") != std::wstring::npos;
    }
    return false;
}

std::wstring trayStatusActionLabel(std::string_view actionName, bool checked) {
    if (actionName == "punctuation") {
        return checked ? L"\x4e2d\x6587\x6807\x70b9\xff1a\x5f00"
                       : L"\x4e2d\x6587\x6807\x70b9\xff1a\x5173";
    }
    if (actionName == "fullwidth") {
        return checked ? L"\x5168\x89d2\x8f93\x5165\xff1a\x5f00"
                       : L"\x5168\x89d2\x8f93\x5165\xff1a\x5173";
    }
    if (actionName == "chttrans") {
        return checked ? L"\x7e41\x4f53\x4e2d\x6587\xff1a\x5f00"
                       : L"\x7e41\x4f53\x4e2d\x6587\xff1a\x5173";
    }
    return {};
}

bool envTruthy(const char *v) { return v && v[0] && std::strcmp(v, "0") != 0; }

/** User env from "System Properties" is in HKCU\\Environment; child processes
 * only see it after explorer restarts. Read registry here so
 * set-fcitx-ts-disable-rime.ps1 works immediately. */
bool userPersistentEnvWideTruthy(const wchar_t *valueName) {
    wchar_t buf[64];
    DWORD size = sizeof(buf);
    DWORD typ = 0;
    const LSTATUS st =
        RegGetValueW(HKEY_CURRENT_USER, L"Environment", valueName,
                     RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &typ, buf, &size);
    if (st != ERROR_SUCCESS) {
        return false;
    }
    if (buf[0] == L'\0' || wcscmp(buf, L"0") == 0) {
        return false;
    }
    return true;
}

bool disableRimeDueToUserEnvOrRegistry() {
    if (envTruthy(std::getenv("FCITX_TS_DISABLE_RIME"))) {
        return true;
    }
    return userPersistentEnvWideTruthy(L"FCITX_TS_DISABLE_RIME");
}

std::filesystem::path dllInstallRootFromAddress(const void *addr) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(addr), &mod) ||
        !mod) {
        return {};
    }
    std::wstring buf(MAX_PATH, L'\0');
    const DWORD r =
        GetModuleFileNameW(mod, buf.data(), static_cast<DWORD>(buf.size()));
    if (!r) {
        return {};
    }
    buf.resize(r);
    std::filesystem::path p(buf);
    return p.parent_path().parent_path();
}

bool currentProcessIsExplorerForTsf() {
    return currentProcessExeBaseNameEquals(L"explorer.exe");
}

bool currentProcessIsQQForTsf() {
    return currentProcessExeBaseNameEquals(L"QQ.exe");
}

bool currentProcessIsCursorForTsf() {
    return currentProcessExeBaseNameEquals(L"Cursor.exe") ||
           currentProcessExeBaseNameEquals(L"Code.exe");
}

struct InstanceArgv {
    std::vector<std::string> storage;
    std::vector<char *> argv;

    explicit InstanceArgv(bool disableRimeForHost) {
        storage.emplace_back("fcitx5");
        if (disableRimeForHost) {
            storage.emplace_back("--disable=rime");
        }
        argv.reserve(storage.size());
        for (auto &arg : storage) {
            argv.push_back(arg.data());
        }
    }

    int argc() const { return static_cast<int>(argv.size()); }
    char **data() { return argv.empty() ? nullptr : argv.data(); }
};

bool shouldDisableRimeForCurrentHost() {
    // Rime loads native librime / deps; mis-matched DLLs or broken deploy can
    // AV the host. FCITX_TS_DISABLE_RIME=1 (process env or HKCU\Environment)
    // adds
    // --disable=rime for diagnosis (pinyin/table still work).
    if (disableRimeDueToUserEnvOrRegistry()) {
        return true;
    }
    return currentProcessIsQQForTsf() || currentProcessIsCursorForTsf();
}

std::filesystem::path imeBinDir() {
    auto root =
        dllInstallRootFromAddress(reinterpret_cast<const void *>(&utf8ToWide));
    if (root.empty()) {
        return {};
    }
    return (root / "bin").lexically_normal();
}

class ScopedDllDirectory {
  public:
    explicit ScopedDllDirectory(const std::filesystem::path &dir) {
        if (dir.empty()) {
            return;
        }
        active_ = SetDllDirectoryW(dir.wstring().c_str()) != 0;
    }

    ~ScopedDllDirectory() {
        if (active_) {
            SetDllDirectoryW(nullptr);
        }
    }

  private:
    bool active_ = false;
};

void setupDefaultTsLogPath() {
    // TSF runs inside arbitrary host processes. Do not set up FCITX_TS_LOG
    // here: attaching glog in transient hosts has caused unload-time crashes.
}

// Install root comes from GetModuleFileNameW → native wchar_t paths.
// path::string() uses the system ACP, but fcitx setEnvironment() interprets
// values as UTF-8. Mismatch breaks FCITX_ADDON_DIRS / LIBIME_MODEL_DIRS (no
// zh_CN.lm → bad ranking, or addons not found).
std::string pathUtf8ForFcitxEnv(const std::filesystem::path &p) {
    if (p.empty()) {
        return {};
    }
    const auto &native = p.native();
    if (native.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, native.data(),
                                      static_cast<int>(native.size()), nullptr,
                                      0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, native.data(),
                        static_cast<int>(native.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}

void setupImeFcitxEnvironment() {
    auto root =
        dllInstallRootFromAddress(reinterpret_cast<const void *>(&utf8ToWide));
    if (root.empty()) {
        return;
    }
    auto addon = (root / "lib" / "fcitx5").lexically_normal();
    auto libimeModels = (root / "lib" / "libime").lexically_normal();
    auto share = root / "share";
    auto fcitxdata = share / "fcitx5";
    // TSF runs inside arbitrary host processes such as QQ. Do not prepend the
    // portable bin/ to PATH here: child processes (e.g. crashpad_handler.exe)
    // inherit PATH and may accidentally load our libglog-2.dll. Keep only the
    // addon/data env vars here; DLL directory changes are scoped narrowly
    // around addon initialization in `Fcitx5ImeEngine::init()`.
    const std::string addonU8 = pathUtf8ForFcitxEnv(addon);
    const std::string shareU8 = pathUtf8ForFcitxEnv(share);
    const std::string fcitxdataU8 = pathUtf8ForFcitxEnv(fcitxdata);
    const std::string libimeU8 = pathUtf8ForFcitxEnv(libimeModels);
    setEnvironment("FCITX_ADDON_DIRS", addonU8.c_str());
    setEnvironment("XDG_DATA_DIRS", shareU8.c_str());
    setEnvironment("FCITX_DATA_DIRS", fcitxdataU8.c_str());
    // DefaultLanguageModelResolver loads zh_CN.lm; without LIBIME_MODEL_DIRS,
    // the CMake-time path is wrong under Program Files and ranking collapses.
    setEnvironment("LIBIME_MODEL_DIRS", libimeU8.c_str());
    setupDefaultTsLogPath();
}

void pinStandardPathsToImeModule() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&pinStandardPathsToImeModule), &self) ||
        !self) {
        return;
    }
    mainInstanceHandle = reinterpret_cast<HINSTANCE>(self);
}

KeyStates winModifierStates() {
    KeyStates s;
    if (GetKeyState(VK_SHIFT) < 0) {
        s |= KeyState::Shift;
    }
    if (GetKeyState(VK_CONTROL) < 0) {
        s |= KeyState::Ctrl;
    }
    if (GetKeyState(VK_MENU) < 0) {
        s |= KeyState::Alt;
    }
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) {
        s |= KeyState::Super;
    }
    return s;
}

Key keyFromWindowsVk(unsigned vk, std::uintptr_t lParam, KeyStates st) {
    const LPARAM lp = static_cast<LPARAM>(lParam);
    const bool ext = (lp & (1 << 24)) != 0;
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
    case VK_DECIMAL:
        return Key(FcitxKey_KP_Decimal, st);
    default:
        break;
    }
    if (vk >= static_cast<unsigned>('0') && vk <= static_cast<unsigned>('9')) {
        const bool shiftDown = (st & KeyState::Shift);
        if (shiftDown) {
            KeyStates stOut = st;
            if (stOut & KeyState::Shift) {
                stOut ^= KeyState::Shift;
            }
            switch (vk) {
            case '1':
                return Key(FcitxKey_exclam, stOut);
            case '2':
                return Key(FcitxKey_at, stOut);
            case '3':
                return Key(FcitxKey_numbersign, stOut);
            case '4':
                return Key(FcitxKey_dollar, stOut);
            case '5':
                return Key(FcitxKey_percent, stOut);
            case '6':
                return Key(FcitxKey_asciicircum, stOut);
            case '7':
                return Key(FcitxKey_ampersand, stOut);
            case '8':
                return Key(FcitxKey_asterisk, stOut);
            case '9':
                return Key(FcitxKey_parenleft, stOut);
            case '0':
                return Key(FcitxKey_parenright, stOut);
            default:
                break;
            }
        }
        return Key(static_cast<KeySym>(FcitxKey_0 + (vk - '0')), st);
    }
    if (vk >= static_cast<unsigned>('A') && vk <= static_cast<unsigned>('Z')) {
        wchar_t c = static_cast<wchar_t>(vk);
        const bool shift = (st & KeyState::Shift);
        const bool caps = (st & KeyState::CapsLock);
        if (!(caps ^ shift)) {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
        return Key(static_cast<KeySym>(FcitxKey_a + (c - L'a')), st);
    }
    // US QWERTY OEM keys → keysyms so punctuation / libime see real symbols
    // (not raw VK with FcitxKey_None).
    {
        const bool shiftDown = (st & KeyState::Shift);
        KeyStates stOut = st;
        if (shiftDown && (stOut & KeyState::Shift)) {
            stOut ^= KeyState::Shift;
        }
        switch (vk) {
        case VK_OEM_1:
            return Key(shiftDown ? FcitxKey_colon : FcitxKey_semicolon, stOut);
        case VK_OEM_2:
            return Key(shiftDown ? FcitxKey_question : FcitxKey_slash, stOut);
        case VK_OEM_3:
            return Key(shiftDown ? FcitxKey_asciitilde : FcitxKey_grave, stOut);
        case VK_OEM_4:
            return Key(shiftDown ? FcitxKey_braceleft : FcitxKey_bracketleft,
                       stOut);
        case VK_OEM_5:
            return Key(shiftDown ? FcitxKey_bar : FcitxKey_backslash, stOut);
        case VK_OEM_6:
            return Key(shiftDown ? FcitxKey_braceright : FcitxKey_bracketright,
                       stOut);
        case VK_OEM_7:
            return Key(shiftDown ? FcitxKey_quotedbl : FcitxKey_apostrophe,
                       stOut);
        case VK_OEM_PLUS:
            return Key(shiftDown ? FcitxKey_plus : FcitxKey_equal, stOut);
        case VK_OEM_COMMA:
            return Key(shiftDown ? FcitxKey_less : FcitxKey_comma, stOut);
        case VK_OEM_MINUS:
            return Key(shiftDown ? FcitxKey_underscore : FcitxKey_minus, stOut);
        case VK_OEM_PERIOD:
            return Key(shiftDown ? FcitxKey_greater : FcitxKey_period, stOut);
        case VK_OEM_102:
            return Key(shiftDown ? FcitxKey_bar : FcitxKey_backslash, stOut);
        default:
            break;
        }
    }
    return Key::fromKeyCode(static_cast<int>(vk), st);
}

bool keyListContains(const KeyList &list, const Key &pressed) {
    for (const auto &rule : list) {
        if (rule.check(pressed)) {
            return true;
        }
    }
    return false;
}

bool isChineseToggleVk(unsigned vk) {
    return vk == VK_SPACE && (GetKeyState(VK_CONTROL) & 0x8000);
}

/// Optional TSF IME logging: `Log::setLogStream` is process-global; refcount
/// when multiple engines exist. Lines are UTF-8; converted for
/// OutputDebugStringW.
class FcitxTsfLogBuf final : public std::streambuf {
  public:
    explicit FcitxTsfLogBuf(bool mirrorDebug) : mirrorDebug_(mirrorDebug) {}

    void openFileUtf8(const char *utf8Path) {
        const std::u8string u8(reinterpret_cast<const char8_t *>(utf8Path),
                               reinterpret_cast<const char8_t *>(utf8Path) +
                                   std::strlen(utf8Path));
        const std::filesystem::path p(u8);
        std::error_code ec;
        bool needBom = true;
        if (std::filesystem::exists(p, ec)) {
            needBom = (std::filesystem::file_size(p, ec) == 0);
        }
        file_.open(p, std::ios::out | std::ios::app);
        fileOpen_ = file_.is_open();
        if (fileOpen_ && needBom) {
            static constexpr char bom[] = "\xEF\xBB\xBF";
            file_.sputn(bom, 3);
        }
    }

    bool isUsable() const { return fileOpen_ || mirrorDebug_; }

    ~FcitxTsfLogBuf() override {
        sync();
        if (fileOpen_) {
            file_.close();
        }
    }

    FcitxTsfLogBuf(const FcitxTsfLogBuf &) = delete;
    FcitxTsfLogBuf &operator=(const FcitxTsfLogBuf &) = delete;

  protected:
    int_type overflow(int_type ch) override {
        if (ch == traits_type::eof()) {
            return traits_type::not_eof(ch);
        }
        const char c = traits_type::to_char_type(ch);
        pending_.push_back(c);
        if (c == '\n') {
            flushPending();
        }
        return ch;
    }

    int sync() override {
        flushPending();
        if (fileOpen_) {
            file_.pubsync();
        }
        return 0;
    }

  private:
    void flushPending() {
        if (pending_.empty()) {
            return;
        }
        if (fileOpen_) {
            file_.sputn(pending_.data(),
                        static_cast<std::streamsize>(pending_.size()));
        }
        if (mirrorDebug_) {
            std::wstring w = utf8ToWide(pending_);
            if (!w.empty()) {
                OutputDebugStringW(w.c_str());
            } else {
                OutputDebugStringA(pending_.c_str());
            }
        }
        pending_.clear();
    }

    std::filebuf file_;
    bool fileOpen_ = false;
    const bool mirrorDebug_;
    std::string pending_;
};

int gTsImeLogRef = 0;
std::unique_ptr<FcitxTsfLogBuf> gTsImeLogBuf;
std::unique_ptr<std::ostream> gTsImeLogOStream;

bool tsImeTryAttachLogging() {
    const char *path = std::getenv("FCITX_TS_LOG");
    const bool debugOut = envTruthy(std::getenv("FCITX_TS_LOG_DEBUGOUT"));
    if ((!path || !path[0]) && !debugOut) {
        return false;
    }
    if (gTsImeLogRef > 0) {
        ++gTsImeLogRef;
        return true;
    }

    const char *rule = std::getenv("FCITX_TS_LOG_RULE");
    if (rule && rule[0]) {
        Log::setLogRule(rule);
    } else {
        // key_trace / addons use Debug; without this, FCITX_TS_LOG alone shows
        // almost nothing.
        Log::setLogRule("*=5");
    }

    gTsImeLogBuf = std::make_unique<FcitxTsfLogBuf>(debugOut);
    if (path && path[0]) {
        gTsImeLogBuf->openFileUtf8(path);
    }
    if (!gTsImeLogBuf->isUsable()) {
        gTsImeLogBuf.reset();
        return false;
    }

    ++gTsImeLogRef;
    gTsImeLogOStream = std::make_unique<std::ostream>(gTsImeLogBuf.get());
    Log::setLogStream(*gTsImeLogOStream);
    return true;
}

void tsImeDetachLogging() {
    if (gTsImeLogRef <= 0) {
        return;
    }
    --gTsImeLogRef;
    if (gTsImeLogRef > 0) {
        return;
    }
    Log::setLogStream(std::cerr);
    gTsImeLogOStream.reset();
    gTsImeLogBuf.reset();
}

} // namespace ime_engine_file

using namespace ime_engine_file;

std::unique_ptr<ImeEngine> makeFcitx5ImeEngineAttempt() {
    auto eng = std::make_unique<Fcitx5ImeEngine>();
    if (!eng->init()) {
        return nullptr;
    }
    return eng;
}

Fcitx5ImeEngine::Fcitx5ImeEngine() = default;

Fcitx5ImeEngine::~Fcitx5ImeEngine() {
    ic_.reset();
    if (!pipeSharedHost_) {
        instance_.reset();
    }
    pipeSharedHost_ = nullptr;
    if (loggingAttached_) {
        tsImeDetachLogging();
        loggingAttached_ = false;
    }
}

bool Fcitx5ImeEngine::init() { return initWithInputMethod({}); }

void Fcitx5ImeEngine::pumpEventLoopForUi() {
    if (Instance *inst = instancePtr()) {
        flushLibuvLoopForIme(inst->eventLoop());
    }
}

bool Fcitx5ImeEngine::initWithInputMethod(
    const std::string &preferredInputMethod) {
    pipeSharedHost_ = nullptr;
    loggingAttached_ = false;
    try {
        pinStandardPathsToImeModule();
        setupImeFcitxEnvironment();
        const bool disableRimeForHost = shouldDisableRimeForCurrentHost();
        if (disableRimeForHost) {
            if (disableRimeDueToUserEnvOrRegistry()) {
                tsfTrace(
                    "Fcitx5ImeEngine::init disabling rime (FCITX_TS_DISABLE_"
                    "RIME env or HKCU\\\\Environment)");
            } else {
                tsfTrace("Fcitx5ImeEngine::init disabling rime addon for risky "
                         "host");
            }
        }
        InstanceArgv instanceArgv(disableRimeForHost);
        ScopedDllDirectory scopedDllDirectory(imeBinDir());
        tsfTrace(
            std::string("Fcitx5ImeEngine::init begin FCITX_TS_LOG=") +
            (std::getenv("FCITX_TS_LOG") ? std::getenv("FCITX_TS_LOG") : ""));
        tsfTrace("Fcitx5ImeEngine::init skip FCITX_TS_LOG in TSF host");
        tsfTrace(std::string("Fcitx5ImeEngine::init loggingAttached=") +
                 (loggingAttached_ ? "true" : "false"));
        if (loggingAttached_) {
            if (const char *lp = std::getenv("FCITX_TS_LOG"); lp && lp[0]) {
                FCITX_INFO() << "Fcitx5 TSF log file (FCITX_TS_LOG): " << lp;
            }
        }
        instance_ = std::make_unique<Instance>(instanceArgv.argc(),
                                               instanceArgv.data());
        tsfTrace("Fcitx5ImeEngine::init Instance constructed");
        instancePtr()->addonManager().registerDefaultLoader(nullptr);
        tsfTrace("Fcitx5ImeEngine::init before initialize()");
        instancePtr()->initialize();
        tsfTrace("Fcitx5ImeEngine::init after initialize()");
        tsfTrace("Fcitx5ImeEngine::init instance initialized");

        ic_ = std::make_unique<TsfInputContext>(
            this, instancePtr()->inputContextManager());
        ic_->setEnablePreedit(true);
        ic_->setCapabilityFlags(CapabilityFlags{
            CapabilityFlag::Preedit, CapabilityFlag::FormattedPreedit,
            CapabilityFlag::ClientSideInputPanel});
        ic_->focusIn();
        tsfTrace("Fcitx5ImeEngine::init input context focused");

        activatePreferredInputMethod(preferredInputMethod);
        syncUiFromIc();
        tsfTrace(std::string("Fcitx5ImeEngine::init success current=") +
                 instancePtr()->inputMethod(ic_.get()));
        return true;
    } catch (...) {
        tsfTrace("Fcitx5ImeEngine::init exception");
        if (loggingAttached_) {
            tsImeDetachLogging();
            loggingAttached_ = false;
        }
        ic_.reset();
        instance_.reset();
        return false;
    }
}

bool Fcitx5ImeEngine::initAsPipeSession(Fcitx5ImePipeShared *host) {
    if (!host || !host->instance()) {
        return false;
    }
    pipeSharedHost_ = host;
    instance_.reset();
    loggingAttached_ = false;
    try {
        // Same as in-proc init: addon DLLs resolve from the IME bin directory.
        // Fcitx5ImePipeShared::init()'s ScopedDllDirectory is gone after return.
        ScopedDllDirectory scopedDllDirectory(imeBinDir());
        ic_ = std::make_unique<TsfInputContext>(
            this, host->instance()->inputContextManager());
        ic_->setEnablePreedit(true);
        ic_->setCapabilityFlags(CapabilityFlags{
            CapabilityFlag::Preedit, CapabilityFlag::FormattedPreedit,
            CapabilityFlag::ClientSideInputPanel});
        ic_->focusIn();
        flushLibuvLoopForIme(host->instance()->eventLoop());
        host->instance()->inputMethodManager().refresh();
        flushLibuvLoopForIme(host->instance()->eventLoop());
        activatePreferredInputMethod({}, false);
        syncUiFromIc();
        const std::string curIm = instancePtr()->inputMethod(ic_.get());
        tsfTrace(std::string("Fcitx5ImeEngine::initAsPipeSession ok current=") +
                 curIm);
        if (curIm.empty()) {
            tsfTrace("Fcitx5ImeEngine::initAsPipeSession WARNING: empty current "
                     "IM after activate (group/addon load?)");
        }
        return true;
    } catch (...) {
        tsfTrace("Fcitx5ImeEngine::initAsPipeSession exception");
        ic_.reset();
        pipeSharedHost_ = nullptr;
        return false;
    }
}

bool Fcitx5ImeEngine::rebuildForInputMethod(
    const std::string &preferredInputMethod) {
    tsfTrace("rebuildForInputMethod begin target=" + preferredInputMethod);
    if (pipeSharedHost_) {
        ic_.reset();
        commitQueueUtf8_.clear();
        preeditWide_.clear();
        candidatesWide_.clear();
        highlightIndex_ = 0;
        preeditCaretWide_ = 0;
        if (!pipeSharedHost_->instance()) {
            return false;
        }
        try {
            ScopedDllDirectory scopedDllDirectory(imeBinDir());
            ic_ = std::make_unique<TsfInputContext>(
                this, pipeSharedHost_->instance()->inputContextManager());
            ic_->setEnablePreedit(true);
            ic_->setCapabilityFlags(CapabilityFlags{
                CapabilityFlag::Preedit, CapabilityFlag::FormattedPreedit,
                CapabilityFlag::ClientSideInputPanel});
            ic_->focusIn();
            flushLibuvLoopForIme(pipeSharedHost_->instance()->eventLoop());
            pipeSharedHost_->instance()->inputMethodManager().refresh();
            flushLibuvLoopForIme(pipeSharedHost_->instance()->eventLoop());
            activatePreferredInputMethod(preferredInputMethod, false);
            syncUiFromIc();
            tsfTrace(std::string("rebuildForInputMethod (pipe session) ok target=") +
                     preferredInputMethod);
            return true;
        } catch (...) {
            tsfTrace("rebuildForInputMethod pipe session exception");
            ic_.reset();
            return false;
        }
    }
    ic_.reset();
    instance_.reset();
    commitQueueUtf8_.clear();
    preeditWide_.clear();
    candidatesWide_.clear();
    highlightIndex_ = 0;
    preeditCaretWide_ = 0;
    const bool ok = initWithInputMethod(preferredInputMethod);
    tsfTrace(std::string("rebuildForInputMethod result=") +
             (ok ? "true" : "false") + " target=" + preferredInputMethod);
    return ok;
}

void Fcitx5ImeEngine::ensurePortableImGroupHasEntries() {
    if (!instancePtr()) {
        return;
    }
    auto &imm = instancePtr()->inputMethodManager();
    InputMethodGroup group = imm.currentGroup();
    auto &items = group.inputMethodList();
    bool changed = false;

    auto hasName = [&items](const std::string &n) {
        return std::any_of(
            items.begin(), items.end(),
            [&n](const InputMethodGroupItem &it) { return it.name() == n; });
    };

    if (items.empty()) {
        // fcitx: index 0 is the inactive keyboard slot. If "pinyin" is the only
        // list entry, canTrigger() is false and setCurrentInputMethod("pinyin")
        // returns immediately; if pinyin is at index 0 with keyboard-us also
        // present, setCurrentInputMethod sets isActive false. Put keyboard-us
        // first when we bootstrap the group.
        if (imm.entry("keyboard-us")) {
            items.emplace_back("keyboard-us");
            changed = true;
        }
        if (imm.entry("pinyin")) {
            items.emplace_back("pinyin");
            changed = true;
        }
        if (items.empty()) {
            imm.foreachEntries([&items, &changed](const InputMethodEntry &entry) {
                items.emplace_back(entry.uniqueName());
                changed = true;
                return items.size() < 2;
            });
        }
        if (!items.empty()) {
            if (imm.entry("pinyin") && hasName("pinyin")) {
                group.setDefaultInputMethod("pinyin");
            } else {
                group.setDefaultInputMethod(items.back().name());
            }
            changed = true;
        }
    } else {
        if (imm.entry("keyboard-us") && !hasName("keyboard-us")) {
            items.insert(items.begin(), InputMethodGroupItem("keyboard-us"));
            changed = true;
        }
        if (imm.entry("pinyin") && !hasName("pinyin")) {
            items.emplace_back(InputMethodGroupItem("pinyin"));
            changed = true;
        }
        if (group.defaultInputMethod().empty() && !items.empty()) {
            if (hasName("pinyin")) {
                group.setDefaultInputMethod("pinyin");
            } else {
                group.setDefaultInputMethod(items[0].name());
            }
            changed = true;
        }
    }

    if (!items.empty() && items[0].name() == "pinyin" &&
        imm.entry("keyboard-us") && hasName("keyboard-us")) {
        auto it = std::find_if(
            items.begin(), items.end(), [](const InputMethodGroupItem &x) {
                return x.name() == "keyboard-us";
            });
        if (it != items.end() && it != items.begin()) {
            const InputMethodGroupItem kb = *it;
            items.erase(it);
            items.insert(items.begin(), kb);
            changed = true;
        }
    }

    if (changed) {
        tsfTrace("ensurePortableImGroupHasEntries: fixed empty IM group");
        imm.setGroup(std::move(group));
        imm.save();
    }
}

void Fcitx5ImeEngine::activatePreferredInputMethod(
    const std::string &preferredInputMethod, bool localIm) {
    if (!instancePtr() || !ic_) {
        return;
    }
    ensurePortableImGroupHasEntries();
    auto &imm = instancePtr()->inputMethodManager();
    if (!preferredInputMethod.empty() && imm.entry(preferredInputMethod)) {
        instancePtr()->setCurrentInputMethod(ic_.get(), preferredInputMethod,
                                             localIm);
        syncUiFromIc();
        return;
    }
    if (const char *envIm = std::getenv("FCITX_TS_IM");
        envIm && envIm[0] && imm.entry(std::string(envIm))) {
        instancePtr()->setCurrentInputMethod(ic_.get(), envIm, localIm);
        syncUiFromIc();
        return;
    }
    const auto &group = imm.currentGroup();
    const std::string &def = group.defaultInputMethod();
    if (!def.empty() && imm.entry(def)) {
        instancePtr()->setCurrentInputMethod(ic_.get(), def, localIm);
        syncUiFromIc();
        return;
    }
    // Prefer pinyin as the TSF startup default when the profile has no default.
    if (imm.entry("pinyin")) {
        instancePtr()->setCurrentInputMethod(ic_.get(), "pinyin", localIm);
        syncUiFromIc();
        return;
    }
    for (const auto &item : group.inputMethodList()) {
        if (imm.entry(item.name())) {
            instancePtr()->setCurrentInputMethod(ic_.get(), item.name(), localIm);
            syncUiFromIc();
            return;
        }
    }
    if (imm.entry("keyboard-us")) {
        instancePtr()->setCurrentInputMethod(ic_.get(), "keyboard-us", localIm);
    } else {
        imm.foreachEntries([this, localIm](const InputMethodEntry &e) {
            instancePtr()->setCurrentInputMethod(ic_.get(), e.uniqueName(),
                                                 localIm);
            return false;
        });
    }
    syncUiFromIc();
}

void Fcitx5ImeEngine::enqueueCommitUtf8(std::string text) {
    commitQueueUtf8_.push_back(std::move(text));
}

void Fcitx5ImeEngine::clear() {
    commitQueueUtf8_.clear();
    if (ic_ && ic_->hasFocus()) {
        ic_->reset();
    }
    syncUiFromIc();
}

const std::wstring &Fcitx5ImeEngine::preedit() const { return preeditWide_; }

int Fcitx5ImeEngine::preeditCaretUtf16() const { return preeditCaretWide_; }

const std::vector<std::wstring> &Fcitx5ImeEngine::candidates() const {
    return candidatesWide_;
}

int Fcitx5ImeEngine::highlightIndex() const { return highlightIndex_; }

void Fcitx5ImeEngine::setHighlightIndex(int /*index*/) {
    // Selection is owned by fcitx; use arrow keys via tryForwardCandidateKey.
}

void Fcitx5ImeEngine::syncInputPanelFromIme() { syncUiFromIc(); }

void Fcitx5ImeEngine::syncUiFromIc() {
    if (Instance *inst = instancePtr()) {
        // InputPanel updates are posted via Instance::eventDispatcher() → libuv
        // async; TSF has no exec(), so pump the loop before reading the panel.
        flushLibuvLoopForIme(inst->eventLoop());
    }
    preeditWide_.clear();
    preeditCaretWide_ = 0;
    candidatesWide_.clear();
    highlightIndex_ = 0;
    if (!ic_) {
        return;
    }
    // Addons may set only clientPreedit (keyboard, quickphrase) or only
    // preedit() (e.g. pinyin when Preedit capability is off). TSF draws one
    // composition line.
    const auto &clientTxt = ic_->inputPanel().clientPreedit();
    const auto &serverTxt = ic_->inputPanel().preedit();
    std::string clientU8 = clientTxt.toString();
    std::string serverU8 = serverTxt.toString();
    std::string preU8;
    int cursorBytes = -1;
    if (!clientU8.empty() && !serverU8.empty()) {
        // Pinyin CommitPreview (and similar): client = preview Chinese, server
        // = composing pinyin. Kimpanel can show both; merge for a single TSF
        // string.
        preU8 = std::move(clientU8) + serverU8;
        const int sc = serverTxt.cursor();
        const int maxB = static_cast<int>(preU8.size());
        if (sc < 0) {
            cursorBytes = maxB;
        } else {
            const int base = maxB - static_cast<int>(serverU8.size());
            cursorBytes = base + sc;
            if (cursorBytes > maxB) {
                cursorBytes = maxB;
            }
            if (cursorBytes < 0) {
                cursorBytes = 0;
            }
        }
    } else if (!clientU8.empty()) {
        preU8 = std::move(clientU8);
        cursorBytes = clientTxt.cursor();
    } else {
        preU8 = std::move(serverU8);
        cursorBytes = serverTxt.cursor();
    }
    const bool traceLatinO = (preU8 == "o" || preU8 == "O");
    preeditWide_ = utf8ToWide(preU8);
    if (cursorBytes < 0) {
        preeditCaretWide_ = static_cast<int>(preeditWide_.size());
    } else {
        preeditCaretWide_ = utf8PrefixToWideLen(preU8, cursorBytes);
        const int maxC = static_cast<int>(preeditWide_.size());
        if (preeditCaretWide_ > maxC) {
            preeditCaretWide_ = maxC;
        }
    }
    auto list = ic_->inputPanel().candidateList();
    if (!list || list->empty()) {
        if (traceLatinO) {
            tsfTrace("o-syncUiFromIc preedit=" + preU8 +
                     " clientPreedit=" + clientTxt.toString() +
                     " serverPreedit=" + serverTxt.toString() +
                     " candidates=<empty>");
        }
        return;
    }
    const int n = list->size();
    candidatesWide_.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        candidatesWide_.push_back(
            utf8ToWide(list->candidate(i).text().toString()));
    }
    highlightIndex_ = list->cursorIndex();
    if (highlightIndex_ < 0 ||
        highlightIndex_ >= static_cast<int>(candidatesWide_.size())) {
        highlightIndex_ = 0;
    }
    if (traceLatinO) {
        std::ostringstream ss;
        ss << "o-syncUiFromIc preedit=" << preU8
           << " clientPreedit=" << clientTxt.toString()
           << " serverPreedit=" << serverTxt.toString()
           << " highlight=" << highlightIndex_ << " topCandidates=";
        const int topN = std::min(5, list->size());
        for (int i = 0; i < topN; ++i) {
            if (i) {
                ss << "|";
            }
            ss << list->candidate(i).text().toString();
        }
        tsfTrace(ss.str());
    }
}

bool Fcitx5ImeEngine::sendKeySym(KeySym sym) {
    if (!ic_) {
        return false;
    }
    if (!ic_->hasFocus()) {
        ic_->focusIn();
    }
    KeyEvent ev(ic_.get(), Key(sym), false);
    const bool keyOk = ic_->keyEvent(ev);
    syncUiFromIc();
    if (sym == FcitxKey_o || sym == FcitxKey_O) {
        std::ostringstream ss;
        ss << "o-sendKeySym sym=" << static_cast<unsigned>(sym)
           << " keyEventRet=" << (keyOk ? "true" : "false")
           << " preeditWide=" << preeditWide_.size()
           << " candidatesWide=" << candidatesWide_.size();
        if (!candidatesWide_.empty()) {
            ss << " top0="
               << ic_->inputPanel()
                      .candidateList()
                      ->candidate(0)
                      .text()
                      .toString();
        }
        tsfTrace(ss.str());
    }
    if (loggingAttached_ && instancePtr()) {
        const auto &panel = ic_->inputPanel();
        FCITX_INFO() << "tsf sendKeySym sym=" << static_cast<unsigned>(sym)
                     << " keyEventRet=" << keyOk
                     << " icFocus=" << ic_->hasFocus()
                     << " im=" << instancePtr()->inputMethod(ic_.get())
                     << " clientPreU8="
                     << panel.clientPreedit().toString().size()
                     << " preeditU8=" << panel.preedit().toString().size()
                     << " syncWidePre=" << preeditWide_.size()
                     << " syncCands=" << candidatesWide_.size();
    }
    return true;
}

bool Fcitx5ImeEngine::appendLatinLowercase(wchar_t ch) {
    if (ch >= L'a' && ch <= L'z') {
        sendKeySym(static_cast<KeySym>(FcitxKey_a + (ch - L'a')));
        return true;
    }
    if (ch >= L'A' && ch <= L'Z') {
        sendKeySym(static_cast<KeySym>(FcitxKey_A + (ch - L'A')));
        return true;
    }
    return false;
}

bool Fcitx5ImeEngine::backspace() {
    sendKeySym(FcitxKey_BackSpace);
    return true;
}

void Fcitx5ImeEngine::moveHighlight(int delta) {
    if (delta < 0) {
        sendKeySym(FcitxKey_Up);
    } else if (delta > 0) {
        sendKeySym(FcitxKey_Down);
    }
}

bool Fcitx5ImeEngine::hasCandidate(size_t index) const {
    return index < candidatesWide_.size();
}

std::wstring Fcitx5ImeEngine::candidateText(size_t index) const {
    return hasCandidate(index) ? candidatesWide_[index] : std::wstring();
}

std::wstring Fcitx5ImeEngine::highlightedCandidateText() const {
    return candidateText(static_cast<size_t>(highlightIndex_));
}

std::wstring Fcitx5ImeEngine::drainNextCommit() {
    if (commitQueueUtf8_.empty()) {
        return {};
    }
    std::string u8 = std::move(commitQueueUtf8_.front());
    commitQueueUtf8_.pop_front();
    return utf8ToWide(u8);
}

bool Fcitx5ImeEngine::feedCandidatePick(size_t index) {
    if (!ic_) {
        return false;
    }
    if (!ic_->hasFocus()) {
        ic_->focusIn();
    }
    if (Instance *inst = instancePtr()) {
        flushLibuvLoopForIme(inst->eventLoop());
    }
    auto list = ic_->inputPanel().candidateList();
    if (!list || list->empty() || static_cast<int>(index) >= list->size()) {
        return false;
    }
    list->candidate(static_cast<int>(index)).select(ic_.get());
    syncUiFromIc();
    return true;
}

bool Fcitx5ImeEngine::tryForwardCandidateKey(unsigned vk) {
    if (candidatesWide_.empty()) {
        return false;
    }
    if (vk >= '0' && vk <= '9') {
        const size_t idx = (vk == '0') ? 9 : static_cast<size_t>(vk - '1');
        return feedCandidatePick(idx);
    }
    if (vk == VK_UP) {
        return sendKeySym(FcitxKey_Up);
    }
    if (vk == VK_DOWN) {
        return sendKeySym(FcitxKey_Down);
    }
    if (vk == VK_SPACE) {
        return sendKeySym(FcitxKey_space);
    }
    if (vk == VK_RETURN) {
        return sendKeySym(FcitxKey_Return);
    }
    return false;
}

bool Fcitx5ImeEngine::tryForwardPreeditCommit() {
    if (!ic_) {
        return false;
    }
    if (ic_->inputPanel().clientPreedit().empty() &&
        ic_->inputPanel().preedit().empty()) {
        return false;
    }
    return sendKeySym(FcitxKey_Return);
}

bool Fcitx5ImeEngine::imManagerHotkeyWouldEat(unsigned vk,
                                              std::uintptr_t lParam) const {
    if (!instancePtr() || isChineseToggleVk(vk)) {
        return false;
    }
    const Key k = keyFromWindowsVk(vk, lParam, winModifierStates());
    const auto &gc = instancePtr()->globalConfig();
    if (keyListContains(gc.enumerateGroupForwardKeys(), k) ||
        keyListContains(gc.enumerateGroupBackwardKeys(), k)) {
        return instancePtr()->inputMethodManager().groupCount() >= 2;
    }
    return keyListContains(gc.enumerateForwardKeys(), k) ||
           keyListContains(gc.enumerateBackwardKeys(), k);
}

bool Fcitx5ImeEngine::tryConsumeImManagerHotkey(unsigned vk,
                                                std::uintptr_t lParam) {
    if (!instancePtr() || !ic_ || isChineseToggleVk(vk)) {
        return false;
    }
    const Key k = keyFromWindowsVk(vk, lParam, winModifierStates());
    auto &gc = instancePtr()->globalConfig();

    for (const auto &rule : gc.enumerateForwardKeys()) {
        if (!rule.check(k)) {
            continue;
        }
        ic_->focusIn();
        const std::string before = instancePtr()->inputMethod(ic_.get());
        instancePtr()->enumerate(true);
        if (instancePtr()->inputMethod(ic_.get()) != before) {
            instancePtr()->save();
            syncUiFromIc();
            return true;
        }
        return false;
    }
    for (const auto &rule : gc.enumerateBackwardKeys()) {
        if (!rule.check(k)) {
            continue;
        }
        ic_->focusIn();
        const std::string before = instancePtr()->inputMethod(ic_.get());
        instancePtr()->enumerate(false);
        if (instancePtr()->inputMethod(ic_.get()) != before) {
            instancePtr()->save();
            syncUiFromIc();
            return true;
        }
        return false;
    }
    auto &imm = instancePtr()->inputMethodManager();
    for (const auto &rule : gc.enumerateGroupForwardKeys()) {
        if (!rule.check(k)) {
            continue;
        }
        if (imm.groupCount() < 2) {
            return false;
        }
        imm.enumerateGroup(true);
        instancePtr()->save();
        syncUiFromIc();
        return true;
    }
    for (const auto &rule : gc.enumerateGroupBackwardKeys()) {
        if (!rule.check(k)) {
            continue;
        }
        if (imm.groupCount() < 2) {
            return false;
        }
        imm.enumerateGroup(false);
        instancePtr()->save();
        syncUiFromIc();
        return true;
    }
    return false;
}

bool Fcitx5ImeEngine::fcitxModifierHotkeyUsesFullKeyEvent(unsigned vk) const {
    if (!instancePtr()) {
        return false;
    }
    // Rime uses Ctrl+` to open its schema / option menu. This must be delivered
    // as a full fcitx KeyEvent instead of being treated as an app Ctrl chord.
    if (vk == VK_OEM_3 && (GetKeyState(VK_CONTROL) & 0x8000) &&
        (GetKeyState(VK_MENU) & 0x8000) == 0) {
        return true;
    }
    switch (vk) {
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

bool Fcitx5ImeEngine::deliverFcitxRawKeyEvent(unsigned vk,
                                              std::uintptr_t lParam,
                                              bool isRelease,
                                              std::uint32_t hostKeyboardStateMask) {
    if (!ic_) {
        return false;
    }
    if (!ic_->hasFocus()) {
        ic_->focusIn();
    }
    KeyStates stForKey;
    if (hostKeyboardStateMask == ImeEngine::kFcitxRawKeyUseProcessKeyboardState) {
        stForKey = winModifierStates();
    } else {
        const std::uint32_t m = hostKeyboardStateMask;
        if (m & static_cast<std::uint32_t>(KeyState::Shift)) {
            stForKey |= KeyState::Shift;
        }
        if (m & static_cast<std::uint32_t>(KeyState::CapsLock)) {
            stForKey |= KeyState::CapsLock;
        }
        if (m & static_cast<std::uint32_t>(KeyState::Ctrl)) {
            stForKey |= KeyState::Ctrl;
        }
        if (m & static_cast<std::uint32_t>(KeyState::Alt)) {
            stForKey |= KeyState::Alt;
        }
        if (m & static_cast<std::uint32_t>(KeyState::Super)) {
            stForKey |= KeyState::Super;
        }
        if (m & static_cast<std::uint32_t>(KeyState::NumLock)) {
            stForKey |= KeyState::NumLock;
        }
    }
    const Key k = keyFromWindowsVk(vk, lParam, stForKey);
    KeyEvent ev(ic_.get(), k, isRelease);
    ic_->keyEvent(ev);
    syncUiFromIc();
    return ev.accepted();
}

std::vector<ProfileInputMethodItem>
Fcitx5ImeEngine::profileInputMethods() const {
    std::vector<ProfileInputMethodItem> items;
    if (!instancePtr() || !ic_) {
        return items;
    }
    auto &imm = instancePtr()->inputMethodManager();
    const std::string current = instancePtr()->inputMethod(ic_.get());
    const auto &group = imm.currentGroup();
    items.reserve(group.inputMethodList().size());
    for (const auto &item : group.inputMethodList()) {
        const auto *entry = imm.entry(item.name());
        if (!entry) {
            continue;
        }
        auto displayName = utf8ToWide(entry->name());
        if (displayName.empty()) {
            displayName = utf8ToWide(entry->uniqueName());
        }
        items.push_back(ProfileInputMethodItem{entry->uniqueName(),
                                               std::move(displayName),
                                               entry->uniqueName() == current});
    }
    return items;
}

bool Fcitx5ImeEngine::activateProfileInputMethod(
    const std::string &uniqueName) {
    if (!instancePtr() || !ic_) {
        tsfTrace(
            "activateProfileInputMethod aborted missing instance/ic target=" +
            uniqueName);
        FCITX_WARN()
            << "activateProfileInputMethod aborted: instance/ic missing target="
            << uniqueName << " pid=" << GetCurrentProcessId();
        return false;
    }
    auto &imm = instancePtr()->inputMethodManager();
    const auto *entry = imm.entry(uniqueName);
    if (!entry) {
        tsfTrace("activateProfileInputMethod target not found=" + uniqueName);
        FCITX_WARN() << "activateProfileInputMethod target not found: "
                     << uniqueName << " pid=" << GetCurrentProcessId();
        return false;
    }
    // On-demand IM addons are loaded after init(), so re-enter the portable bin
    // DLL directory here to resolve their runtime dependencies.
    ScopedDllDirectory scopedDllDirectory(imeBinDir());
    const std::string addon = entry->addon();
    auto loadedAddonSummary = [this]() {
        std::string loaded;
        const auto &names = instancePtr()->addonManager().loadedAddonNames();
        for (size_t i = 0; i < names.size(); ++i) {
            if (i != 0) {
                loaded += ",";
            }
            loaded += names[i];
        }
        return loaded;
    };
    if (!addon.empty()) {
        auto *addonInstance = instancePtr()->addonManager().addon(addon, true);
        tsfTrace("activateProfileInputMethod addon request target=" +
                 uniqueName + " addon=" + addon +
                 " loaded=" + std::string(addonInstance ? "true" : "false") +
                 " loadedAddons=" + loadedAddonSummary());
        if (!addonInstance && !rebuildForInputMethod(uniqueName)) {
            tsfTrace("activateProfileInputMethod rebuild failed target=" +
                     uniqueName + " addon=" + addon);
        }
    }
    // Explicitly load the target addon in the current host process first.
    // This is required for on-demand engines like Rime; otherwise the IM name
    // may switch while addonManager never loads `rime` in the focused app.
    auto *targetEngine = instancePtr()->inputMethodEngine(uniqueName);
    if (!targetEngine) {
        tsfTrace("activateProfileInputMethod inputMethodEngine returned null "
                 "target=" +
                 uniqueName + " addon=" + addon +
                 " loadedAddons=" + loadedAddonSummary());
        FCITX_ERROR() << "activateProfileInputMethod failed to load engine for "
                      << uniqueName << " addon=" << addon
                      << " pid=" << GetCurrentProcessId();
        return false;
    }
    tsfTrace("activateProfileInputMethod begin target=" + uniqueName +
             " addon=" + addon +
             " current=" + instancePtr()->inputMethod(ic_.get()));
    FCITX_INFO() << "activateProfileInputMethod begin target=" << uniqueName
                 << " addon=" << addon
                 << " current=" << instancePtr()->inputMethod(ic_.get())
                 << " focused=" << ic_->hasFocus()
                 << " pid=" << GetCurrentProcessId();
    ic_->focusIn();
    // Tray actions run inside the shell host process. Use global switching so
    // the target input method also applies to the currently focused app
    // (e.g. Notepad), instead of only changing explorer.exe's local IC state.
    instancePtr()->setCurrentInputMethod(ic_.get(), uniqueName, false);
    auto *engine = instancePtr()->inputMethodEngine(uniqueName);
    if (!engine || instancePtr()->inputMethod(ic_.get()) != uniqueName) {
        tsfTrace("activateProfileInputMethod switch failed target=" +
                 uniqueName + " addon=" + addon +
                 " current=" + instancePtr()->inputMethod(ic_.get()));
        FCITX_ERROR() << "activateProfileInputMethod switch failed target="
                      << uniqueName << " addon=" << addon
                      << " current=" << instancePtr()->inputMethod(ic_.get())
                      << " engineLoaded=" << (engine != nullptr)
                      << " pid=" << GetCurrentProcessId();
        return false;
    }
    instancePtr()->save();
    syncUiFromIc();
    tsfTrace("activateProfileInputMethod success target=" + uniqueName +
             " addon=" + addon +
             " current=" + instancePtr()->inputMethod(ic_.get()));
    FCITX_INFO() << "activateProfileInputMethod success target=" << uniqueName
                 << " addon=" << addon
                 << " current=" << instancePtr()->inputMethod(ic_.get())
                 << " pid=" << GetCurrentProcessId();
    return true;
}

std::string Fcitx5ImeEngine::currentInputMethod() const {
    if (!instancePtr() || !ic_) {
        return {};
    }
    return instancePtr()->inputMethod(ic_.get());
}

std::vector<TrayStatusActionItem> Fcitx5ImeEngine::trayStatusActions() const {
    std::vector<TrayStatusActionItem> items;
    if (!instancePtr() || !ic_) {
        return items;
    }
    static constexpr const char *kActionNames[] = {"punctuation", "fullwidth",
                                                   "chttrans"};
    items.reserve(std::size(kActionNames));
    for (const char *name : kActionNames) {
        auto *action = lookupTrayStatusAction(instancePtr(), ic_.get(), name);
        if (!action) {
            continue;
        }
        const auto shortText = utf8ToWide(action->shortText(ic_.get()));
        if (shortText.empty()) {
            continue;
        }
        const bool checked = trayStatusActionChecked(name, shortText);
        items.push_back(TrayStatusActionItem{
            action->name(), trayStatusActionLabel(name, checked), checked});
    }
    return items;
}

bool Fcitx5ImeEngine::activateTrayStatusAction(const std::string &uniqueName) {
    if (!instancePtr() || !ic_ || uniqueName.empty()) {
        return false;
    }
    auto *action =
        lookupTrayStatusAction(instancePtr(), ic_.get(), uniqueName.c_str());
    if (!action) {
        return false;
    }
    ic_->focusIn();
    action->activate(ic_.get());
    flushLibuvLoopForIme(instancePtr()->eventLoop());
    instancePtr()->save();
    syncUiFromIc();
    return true;
}

bool Fcitx5ImeEngine::reloadPinyinConfig() {
    if (!instancePtr()) {
        return false;
    }
    ScopedDllDirectory scopedDllDirectory(imeBinDir());
    instancePtr()->reloadAddonConfig("pinyin");
    flushLibuvLoopForIme(instancePtr()->eventLoop());
    if (ic_) {
        syncUiFromIc();
    }
    return true;
}

bool Fcitx5ImeEngine::invokeInputMethodSubConfig(const std::string &uniqueName,
                                                 const std::string &subPath) {
    if (!instancePtr() || uniqueName.empty() || subPath.empty()) {
        return false;
    }
    ScopedDllDirectory scopedDllDirectory(imeBinDir());
    auto *engine = instancePtr()->inputMethodEngine(uniqueName);
    if (!engine) {
        return false;
    }
    RawConfig config;
    engine->setSubConfig(subPath, config);
    return true;
}

bool Fcitx5ImePipeShared::init() {
    try {
        pinStandardPathsToImeModule();
        setupImeFcitxEnvironment();
        const bool disableRimeForHost = shouldDisableRimeForCurrentHost();
        if (disableRimeForHost) {
            if (disableRimeDueToUserEnvOrRegistry()) {
                tsfTrace(
                    "Fcitx5ImePipeShared::init disabling rime (FCITX_TS_DISABLE_"
                    "RIME env or HKCU\\\\Environment)");
            } else {
                tsfTrace("Fcitx5ImePipeShared::init disabling rime addon for "
                         "risky host");
            }
        }
        InstanceArgv instanceArgv(disableRimeForHost);
        ScopedDllDirectory scopedDllDirectory(imeBinDir());
        instance_ = std::make_unique<Instance>(instanceArgv.argc(),
                                               instanceArgv.data());
        tsfTrace("Fcitx5ImePipeShared::init Instance constructed");
        instance_->addonManager().registerDefaultLoader(nullptr);
        tsfTrace("Fcitx5ImePipeShared::init before initialize()");
        instance_->initialize();
        tsfTrace("Fcitx5ImePipeShared::init after initialize()");
        flushLibuvLoopForIme(instance_->eventLoop());
        return true;
    } catch (...) {
        tsfTrace("Fcitx5ImePipeShared::init exception");
        instance_.reset();
        return false;
    }
}

} // namespace fcitx
