#include "Fcitx5ImeEngine.h"
#include "TsfInputContext.h"

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/environ.h>
#include <fcitx-utils/key.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx/globalconfig.h>
#include <fcitx/instance.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>

#include <fcitx-utils/log.h>

#include <Windows.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <streambuf>
#include <string>

namespace fcitx {

// Fcitx5Utils (standardpaths_p_win.cpp): pin portable layout to the IME DLL, not
// the host process .exe (Notepad, etc.).
extern HINSTANCE mainInstanceHandle;

namespace {

std::wstring utf8ToWide(const std::string &utf8) {
    if (utf8.empty()) {
        return {};
    }
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) {
        return {};
    }
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        w.data(), n);
    return w;
}

std::filesystem::path dllInstallRootFromAddress(const void *addr) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
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

void setupImeFcitxEnvironment() {
    auto root = dllInstallRootFromAddress(
        reinterpret_cast<const void *>(&utf8ToWide));
    if (root.empty()) {
        return;
    }
    auto addon = root / "lib" / "fcitx5";
    auto share = root / "share";
    auto fcitxdata = share / "fcitx5";
    setEnvironment("FCITX_ADDON_DIRS", addon.string().c_str());
    setEnvironment("XDG_DATA_DIRS", share.string().c_str());
    setEnvironment("FCITX_DATA_DIRS", fcitxdata.string().c_str());
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

Key keyFromWindowsVk(unsigned vk, std::uintptr_t lParam) {
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
        return Key(static_cast<KeySym>(FcitxKey_0 + (vk - '0')), st);
    }
    if (vk >= static_cast<unsigned>('A') && vk <= static_cast<unsigned>('Z')) {
        wchar_t c = static_cast<wchar_t>(vk);
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
        if (!(caps ^ shift)) {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
        return Key(static_cast<KeySym>(FcitxKey_a + (c - L'a')), st);
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

bool envTruthy(const char *v) {
    return v && v[0] && std::strcmp(v, "0") != 0;
}

/// Optional TSF IME logging: `Log::setLogStream` is process-global; refcount when
/// multiple engines exist. Lines are UTF-8; converted for OutputDebugStringW.
class FcitxTsfLogBuf final : public std::streambuf {
public:
    explicit FcitxTsfLogBuf(bool mirrorDebug) : mirrorDebug_(mirrorDebug) {}

    void openFileUtf8(const char *utf8Path) {
        const std::u8string u8(reinterpret_cast<const char8_t *>(utf8Path),
                               reinterpret_cast<const char8_t *>(utf8Path) +
                                   std::strlen(utf8Path));
        const std::filesystem::path p(u8);
        file_.open(p, std::ios::out | std::ios::app);
        fileOpen_ = file_.is_open();
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

} // namespace

std::unique_ptr<ImeEngine> makeFcitx5ImeEngineAttempt() {
    auto eng = std::make_unique<Fcitx5ImeEngine>();
    if (!eng->init()) {
        return nullptr;
    }
    return eng;
}

Fcitx5ImeEngine::Fcitx5ImeEngine() = default;

Fcitx5ImeEngine::~Fcitx5ImeEngine() {
    if (dispatcher_) {
        dispatcher_->detach();
    }
    ic_.reset();
    dispatcher_.reset();
    instance_.reset();
    if (loggingAttached_) {
        tsImeDetachLogging();
        loggingAttached_ = false;
    }
}

bool Fcitx5ImeEngine::init() {
    loggingAttached_ = false;
    try {
        pinStandardPathsToImeModule();
        setupImeFcitxEnvironment();
        loggingAttached_ = tsImeTryAttachLogging();
        instance_ = std::make_unique<Instance>(0, nullptr);
        instance_->addonManager().registerDefaultLoader(nullptr);
        instance_->initialize();

        dispatcher_ = std::make_unique<EventDispatcher>();
        dispatcher_->attach(&instance_->eventLoop());

        ic_ = std::make_unique<TsfInputContext>(this,
                                                instance_->inputContextManager());
        ic_->setCapabilityFlags(
            CapabilityFlags{CapabilityFlag::Preedit, CapabilityFlag::FormattedPreedit,
                            CapabilityFlag::ClientSideInputPanel});
        ic_->focusIn();

        activatePreferredInputMethod();
        syncUiFromIc();
        return true;
    } catch (...) {
        if (loggingAttached_) {
            tsImeDetachLogging();
            loggingAttached_ = false;
        }
        ic_.reset();
        dispatcher_.reset();
        instance_.reset();
        return false;
    }
}

void Fcitx5ImeEngine::activatePreferredInputMethod() {
    if (!instance_ || !ic_) {
        return;
    }
    auto &imm = instance_->inputMethodManager();
    if (const char *envIm = std::getenv("FCITX_TS_IM");
        envIm && envIm[0] && imm.entry(std::string(envIm))) {
        instance_->setCurrentInputMethod(ic_.get(), envIm, true);
        syncUiFromIc();
        return;
    }
    const auto &group = imm.currentGroup();
    const std::string &def = group.defaultInputMethod();
    if (!def.empty() && imm.entry(def)) {
        instance_->setCurrentInputMethod(ic_.get(), def, true);
        syncUiFromIc();
        return;
    }
    for (const auto &item : group.inputMethodList()) {
        if (imm.entry(item.name())) {
            instance_->setCurrentInputMethod(ic_.get(), item.name(), true);
            syncUiFromIc();
            return;
        }
    }
    if (imm.entry("keyboard-us")) {
        instance_->setCurrentInputMethod(ic_.get(), "keyboard-us", true);
    } else {
        imm.foreachEntries([this](const InputMethodEntry &e) {
            instance_->setCurrentInputMethod(ic_.get(), e.uniqueName(), true);
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

const std::vector<std::wstring> &Fcitx5ImeEngine::candidates() const {
    return candidatesWide_;
}

int Fcitx5ImeEngine::highlightIndex() const { return highlightIndex_; }

void Fcitx5ImeEngine::setHighlightIndex(int /*index*/) {
    // Selection is owned by fcitx; use arrow keys via tryForwardCandidateKey.
}

void Fcitx5ImeEngine::syncUiFromIc() {
    preeditWide_.clear();
    candidatesWide_.clear();
    highlightIndex_ = 0;
    if (!ic_) {
        return;
    }
    preeditWide_ = utf8ToWide(ic_->inputPanel().clientPreedit().toString());
    auto list = ic_->inputPanel().candidateList();
    if (!list || list->empty()) {
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
}

bool Fcitx5ImeEngine::sendKeySym(KeySym sym) {
    if (!ic_) {
        return false;
    }
    if (!ic_->hasFocus()) {
        ic_->focusIn();
    }
    KeyEvent ev(ic_.get(), Key(sym), false);
    ic_->keyEvent(ev);
    syncUiFromIc();
    return true;
}

void Fcitx5ImeEngine::appendLatinLowercase(wchar_t ch) {
    if (ch < L'a' || ch > L'z') {
        return;
    }
    const auto sym = static_cast<KeySym>(FcitxKey_a + (ch - L'a'));
    sendKeySym(sym);
}

void Fcitx5ImeEngine::backspace() { sendKeySym(FcitxKey_BackSpace); }

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
    if (!hasCandidate(index)) {
        return false;
    }
    const KeySym sym =
        (index == 9) ? FcitxKey_0
                     : static_cast<KeySym>(FcitxKey_1 + static_cast<KeySym>(index));
    sendKeySym(sym);
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
    if (!ic_ || ic_->inputPanel().clientPreedit().empty()) {
        return false;
    }
    return sendKeySym(FcitxKey_Return);
}

bool Fcitx5ImeEngine::imManagerHotkeyWouldEat(unsigned vk,
                                              std::uintptr_t lParam) const {
    if (!instance_ || isChineseToggleVk(vk)) {
        return false;
    }
    const Key k = keyFromWindowsVk(vk, lParam);
    const auto &gc = instance_->globalConfig();
    if (keyListContains(gc.enumerateGroupForwardKeys(), k) ||
        keyListContains(gc.enumerateGroupBackwardKeys(), k)) {
        return instance_->inputMethodManager().groupCount() >= 2;
    }
    return keyListContains(gc.enumerateForwardKeys(), k) ||
           keyListContains(gc.enumerateBackwardKeys(), k);
}

bool Fcitx5ImeEngine::tryConsumeImManagerHotkey(unsigned vk,
                                                std::uintptr_t lParam) {
    if (!instance_ || !ic_ || isChineseToggleVk(vk)) {
        return false;
    }
    const Key k = keyFromWindowsVk(vk, lParam);
    auto &gc = instance_->globalConfig();

    for (const auto &rule : gc.enumerateForwardKeys()) {
        if (!rule.check(k)) {
            continue;
        }
        ic_->focusIn();
        const std::string before = instance_->inputMethod(ic_.get());
        instance_->enumerate(true);
        if (instance_->inputMethod(ic_.get()) != before) {
            instance_->save();
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
        const std::string before = instance_->inputMethod(ic_.get());
        instance_->enumerate(false);
        if (instance_->inputMethod(ic_.get()) != before) {
            instance_->save();
            syncUiFromIc();
            return true;
        }
        return false;
    }
    auto &imm = instance_->inputMethodManager();
    for (const auto &rule : gc.enumerateGroupForwardKeys()) {
        if (!rule.check(k)) {
            continue;
        }
        if (imm.groupCount() < 2) {
            return false;
        }
        imm.enumerateGroup(true);
        instance_->save();
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
        instance_->save();
        syncUiFromIc();
        return true;
    }
    return false;
}

} // namespace fcitx
