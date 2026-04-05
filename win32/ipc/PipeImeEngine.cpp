#include "PipeImeEngine.h"

#include "Fcitx5ImeIpcCodec.h"

#include "TsfTrace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace fcitx {
namespace {

HANDLE pipeServerLaunchMutexHandle() {
    static HANDLE h = nullptr;
    if (h) {
        return h;
    }
    h = CreateMutexW(nullptr, FALSE, imeIpcPipeServerLaunchMutexName().c_str());
    return h;
}

std::wstring utf8ToWide(std::string_view u8) {
    if (u8.empty()) {
        return {};
    }
    const int wlen =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u8.data(),
                            static_cast<int>(u8.size()), nullptr, 0);
    if (wlen <= 0) {
        const int wlen2 = MultiByteToWideChar(
            CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()), nullptr, 0);
        if (wlen2 <= 0) {
            return {};
        }
        std::wstring w(static_cast<size_t>(wlen2), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()),
                            w.data(), wlen2);
        return w;
    }
    std::wstring w(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u8.data(),
                        static_cast<int>(u8.size()), w.data(), wlen);
    return w;
}

std::filesystem::path imePipeServerExePath() {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&makePipeImeEngineAttempt), &mod) ||
        !mod) {
        return {};
    }
    std::wstring buf(MAX_PATH, L'\0');
    const DWORD r =
        GetModuleFileNameW(mod, buf.data(), static_cast<DWORD>(buf.size()));
    if (r == 0) {
        return {};
    }
    buf.resize(r);
    const auto root = std::filesystem::path(buf).parent_path().parent_path();
    return root / "bin" / "Fcitx5ImePipeServer.exe";
}

} // namespace

void PipeImeEngine::appendU32(std::vector<std::uint8_t> &b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
}

void PipeImeEngine::appendU64(std::vector<std::uint8_t> &b, std::uint64_t v) {
    appendU32(b, static_cast<std::uint32_t>(v & 0xffffffffu));
    appendU32(b, static_cast<std::uint32_t>(v >> 32));
}

void PipeImeEngine::appendUtf8(std::vector<std::uint8_t> &b,
                               const std::string &s) {
    appendU32(b, static_cast<std::uint32_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

PipeImeEngine::PipeImeEngine() = default;

PipeImeEngine::~PipeImeEngine() {
    std::lock_guard<std::mutex> lock(mutex_);
    closePipeUnlocked();
}

void PipeImeEngine::closePipeUnlocked() const {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool PipeImeEngine::tryLaunchPipeServerProcess() const {
    HANDLE mx = pipeServerLaunchMutexHandle();
    if (!mx) {
        return false;
    }
    const DWORD w = WaitForSingleObject(mx, 60000);
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
        return false;
    }
    const std::wstring path = imeIpcNamedPipePath();
    HANDLE probe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                               nullptr, OPEN_EXISTING, 0, nullptr);
    if (probe != INVALID_HANDLE_VALUE) {
        CloseHandle(probe);
        ReleaseMutex(mx);
        return true;
    }
    const auto exe = imePipeServerExePath();
    if (exe.empty()) {
        tsfTrace("PipeImeEngine: cannot resolve Fcitx5ImePipeServer.exe path");
        ReleaseMutex(mx);
        return false;
    }
    std::wstring quoted = L"\"";
    quoted += exe.wstring();
    quoted += L"\"";
    std::vector<wchar_t> cmdline(quoted.begin(), quoted.end());
    cmdline.push_back(L'\0');
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    const BOOL ok =
        CreateProcessW(exe.wstring().c_str(), cmdline.data(), nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                       nullptr, nullptr, &si, &pi);
    if (!ok) {
        tsfTrace("PipeImeEngine: CreateProcessW Fcitx5ImePipeServer failed");
        ReleaseMutex(mx);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    for (int i = 0; i < 100; ++i) {
        Sleep(50);
        probe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_EXISTING, 0, nullptr);
        if (probe != INVALID_HANDLE_VALUE) {
            CloseHandle(probe);
            break;
        }
    }
    ReleaseMutex(mx);
    return true;
}

bool PipeImeEngine::ensurePipeConnectedUnlocked() const {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        return true;
    }
    const std::wstring path = imeIpcNamedPipePath();
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (attempt > 0) {
            Sleep(static_cast<DWORD>(40 * attempt));
        }
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            pipe_ = h;
            return true;
        }
        if (attempt == 0) {
            tryLaunchPipeServerProcess();
        }
    }
    tsfTrace("PipeImeEngine: failed to connect named pipe");
    return false;
}

bool PipeImeEngine::transact(ImeIpcOpcode op,
                             const std::vector<std::uint8_t> &body) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensurePipeConnectedUnlocked()) {
        tsfTrace("PipeImeEngine: transact no pipe opcode=" +
                 std::to_string(static_cast<unsigned>(op)));
        return false;
    }
    const std::vector<std::uint8_t> packet = imeIpcEncodeRequest(op, body);
    if (packet.size() > kImeIpcMaxPacket) {
        return false;
    }
    if (!imeIpcWriteAll(pipe_, packet.data(), packet.size())) {
        closePipeUnlocked();
        return false;
    }
    ImeIpcFrameHeader rh = {};
    if (!imeIpcReadAll(pipe_, &rh, sizeof(rh))) {
        closePipeUnlocked();
        return false;
    }
    if (rh.magic != kImeIpcFrameMagic || rh.version != kImeIpcVersion ||
        rh.bodySize > kImeIpcMaxPacket) {
        closePipeUnlocked();
        return false;
    }
    std::vector<std::uint8_t> resp(sizeof(rh) + rh.bodySize);
    std::memcpy(resp.data(), &rh, sizeof(rh));
    if (rh.bodySize > 0) {
        if (!imeIpcReadAll(pipe_, resp.data() + sizeof(rh), rh.bodySize)) {
            closePipeUnlocked();
            return false;
        }
    }
    ImeIpcDecoded dec = {};
    if (!imeIpcDecodeResponsePacket(resp, &dec)) {
        tsfTrace("PipeImeEngine: decode response failed opcode=" +
                 std::to_string(static_cast<unsigned>(op)));
        closePipeUnlocked();
        return false;
    }
    lastFlags_ = dec.flags;
    lastDrainedCommitUtf8_ = std::move(dec.drainedCommitUtf8);
    applySnapshot(dec);
    return true;
}

void PipeImeEngine::applySnapshot(const ImeIpcDecoded &d) const {
    preedit_ = d.preedit;
    candidates_ = d.candidates;
    highlight_ = d.highlight;
    caretUtf16_ = d.caretUtf16;
    currentImUtf8_ = d.currentImUtf8;
    profileIms_ = d.profileIms;
    trayActions_ = d.trayActions;
}

bool PipeImeEngine::pingConnect() { return transact(ImeIpcOpcode::Ping, {}); }

std::unique_ptr<ImeEngine> makePipeImeEngineAttempt() {
    auto p = std::make_unique<PipeImeEngine>();
    if (!p->pingConnect()) {
        return nullptr;
    }
    return p;
}

void PipeImeEngine::clear() { transact(ImeIpcOpcode::Clear, {}); }

void PipeImeEngine::syncInputPanelFromIme() {
    transact(ImeIpcOpcode::SyncInputPanel, {});
}

const std::wstring &PipeImeEngine::preedit() const { return preedit_; }

int PipeImeEngine::preeditCaretUtf16() const { return caretUtf16_; }

const std::vector<std::wstring> &PipeImeEngine::candidates() const {
    return candidates_;
}

int PipeImeEngine::highlightIndex() const { return highlight_; }

void PipeImeEngine::setHighlightIndex(int index) {
    std::vector<std::uint8_t> b;
    appendU32(b, static_cast<std::uint32_t>(index));
    transact(ImeIpcOpcode::SetHighlight, b);
}

bool PipeImeEngine::appendLatinLowercase(wchar_t ch) {
    std::vector<std::uint8_t> b;
    appendU32(b, static_cast<std::uint32_t>(ch));
    return transact(ImeIpcOpcode::AppendLatin, b);
}

bool PipeImeEngine::backspace() {
    return transact(ImeIpcOpcode::Backspace, {});
}

void PipeImeEngine::moveHighlight(int delta) {
    std::vector<std::uint8_t> b;
    appendU32(b, static_cast<std::uint32_t>(static_cast<std::int32_t>(delta)));
    transact(ImeIpcOpcode::MoveHighlight, b);
}

bool PipeImeEngine::hasCandidate(size_t index) const {
    return index < candidates_.size();
}

std::wstring PipeImeEngine::candidateText(size_t index) const {
    return hasCandidate(index) ? candidates_[index] : std::wstring();
}

std::wstring PipeImeEngine::highlightedCandidateText() const {
    if (highlight_ >= 0 && highlight_ < static_cast<int>(candidates_.size())) {
        return candidates_[static_cast<size_t>(highlight_)];
    }
    return {};
}

std::wstring PipeImeEngine::drainNextCommit() {
    std::vector<std::uint8_t> empty;
    if (!transact(ImeIpcOpcode::ServerPopCommit, empty)) {
        return {};
    }
    std::string u8;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        u8 = std::move(lastDrainedCommitUtf8_);
        lastDrainedCommitUtf8_.clear();
    }
    return utf8ToWide(u8);
}

bool PipeImeEngine::feedCandidatePick(size_t index) {
    std::vector<std::uint8_t> b;
    appendU32(b, static_cast<std::uint32_t>(index));
    return transact(ImeIpcOpcode::FeedCandidatePick, b);
}

bool PipeImeEngine::tryForwardCandidateKey(unsigned vk) {
    std::vector<std::uint8_t> b;
    appendU32(b, vk);
    if (!transact(ImeIpcOpcode::TryForwardCandidateKey, b)) {
        return false;
    }
    return (lastFlags_ & 1u) != 0;
}

bool PipeImeEngine::tryForwardPreeditCommit() {
    if (!transact(ImeIpcOpcode::TryForwardPreeditCommit, {})) {
        return false;
    }
    return (lastFlags_ & 1u) != 0;
}

bool PipeImeEngine::imManagerHotkeyWouldEat(unsigned vk,
                                            std::uintptr_t lParam) const {
    std::vector<std::uint8_t> b;
    appendU32(b, vk);
    appendU64(b, static_cast<std::uint64_t>(lParam));
    if (!transact(ImeIpcOpcode::ImManagerHotkeyWouldEat, b)) {
        return false;
    }
    return (lastFlags_ & 2u) != 0;
}

bool PipeImeEngine::tryConsumeImManagerHotkey(unsigned vk,
                                              std::uintptr_t lParam) {
    std::vector<std::uint8_t> b;
    appendU32(b, vk);
    appendU64(b, static_cast<std::uint64_t>(lParam));
    if (!transact(ImeIpcOpcode::TryConsumeImManagerHotkey, b)) {
        return false;
    }
    return (lastFlags_ & 1u) != 0;
}

bool PipeImeEngine::fcitxModifierHotkeyUsesFullKeyEvent(unsigned vk) const {
    std::vector<std::uint8_t> body;
    appendU32(body, vk);
    if (!transact(ImeIpcOpcode::FcitxModifierHotkeyUsesFullKeyEvent, body)) {
        return false;
    }
    return (lastFlags_ & 4u) != 0;
}

bool PipeImeEngine::deliverFcitxRawKeyEvent(
    unsigned vk, std::uintptr_t lParam, bool isRelease,
    std::uint32_t hostKeyboardStateMask) {
    std::vector<std::uint8_t> b;
    appendU32(b, vk);
    appendU64(b, static_cast<std::uint64_t>(lParam));
    appendU32(b, isRelease ? 1u : 0u);
    appendU32(b, hostKeyboardStateMask);
    if (!transact(ImeIpcOpcode::DeliverFcitxRawKeyEvent, b)) {
        return false;
    }
    return (lastFlags_ & 1u) != 0;
}

bool PipeImeEngine::usesHostKeyboardStateForRawKeyDelivery() const {
    return true;
}

std::vector<ProfileInputMethodItem> PipeImeEngine::profileInputMethods() const {
    transact(ImeIpcOpcode::Ping, {});
    return profileIms_;
}

bool PipeImeEngine::activateProfileInputMethod(const std::string &uniqueName) {
    std::vector<std::uint8_t> b;
    appendUtf8(b, uniqueName);
    return transact(ImeIpcOpcode::ActivateProfileInputMethod, b);
}

std::string PipeImeEngine::currentInputMethod() const {
    transact(ImeIpcOpcode::Ping, {});
    return currentImUtf8_;
}

std::vector<TrayStatusActionItem> PipeImeEngine::trayStatusActions() const {
    transact(ImeIpcOpcode::Ping, {});
    return trayActions_;
}

bool PipeImeEngine::activateTrayStatusAction(const std::string &uniqueName) {
    std::vector<std::uint8_t> b;
    appendUtf8(b, uniqueName);
    return transact(ImeIpcOpcode::ActivateTrayStatusAction, b);
}

bool PipeImeEngine::reloadPinyinConfig() {
    return transact(ImeIpcOpcode::ReloadPinyinConfig, {});
}

bool PipeImeEngine::reloadRimeAddonConfig() {
    return transact(ImeIpcOpcode::ReloadRimeConfig, {});
}

bool PipeImeEngine::invokeInputMethodSubConfig(const std::string &uniqueName,
                                               const std::string &subPath) {
    std::vector<std::uint8_t> b;
    appendUtf8(b, uniqueName);
    appendUtf8(b, subPath);
    return transact(ImeIpcOpcode::InvokeInputMethodSubConfig, b);
}

} // namespace fcitx
