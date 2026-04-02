#include "Fcitx5ImeEngine.h"
#include "Fcitx5ImePipeShared.h"
#include "Fcitx5ImeIpcCodec.h"
#include "Fcitx5ImeIpcProtocol.h"
#include "ImeEngine.h"
#include "TsfTrace.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using fcitx::ImeEngine;
using fcitx::ImeIpcOpcode;

bool eatU32(const std::uint8_t *&p, const std::uint8_t *e, std::uint32_t *o) {
    if (static_cast<std::size_t>(e - p) < 4) {
        return false;
    }
    *o = static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
    p += 4;
    return true;
}

bool eatU64(const std::uint8_t *&p, const std::uint8_t *e, std::uint64_t *o) {
    std::uint32_t lo = 0, hi = 0;
    if (!eatU32(p, e, &lo) || !eatU32(p, e, &hi)) {
        return false;
    }
    *o = (static_cast<std::uint64_t>(hi) << 32) | lo;
    return true;
}

bool eatUtf8(const std::uint8_t *&p, const std::uint8_t *e, std::string *s) {
    std::uint32_t len = 0;
    if (!eatU32(p, e, &len) || len > 256 * 1024 ||
        static_cast<std::size_t>(e - p) < len) {
        return false;
    }
    s->assign(reinterpret_cast<const char *>(p),
              reinterpret_cast<const char *>(p + len));
    p += len;
    return true;
}

std::vector<std::uint8_t> handleRequest(ImeEngine *eng, ImeIpcOpcode op,
                                        const std::uint8_t *body,
                                        std::size_t bodySize) {
    const std::uint8_t *p = body;
    const std::uint8_t *end = body + bodySize;
    std::uint32_t flags = 0;
    bool drain = false;

    switch (op) {
    case ImeIpcOpcode::Ping:
        break;
    case ImeIpcOpcode::SyncInputPanel:
        eng->syncInputPanelFromIme();
        break;
    case ImeIpcOpcode::Clear:
        eng->clear();
        break;
    case ImeIpcOpcode::AppendLatin: {
        std::uint32_t ch = 0;
        if (!eatU32(p, end, &ch) || p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        eng->appendLatinLowercase(static_cast<wchar_t>(ch));
        break;
    }
    case ImeIpcOpcode::Backspace:
        if (p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        eng->backspace();
        break;
    case ImeIpcOpcode::MoveHighlight: {
        std::uint32_t d = 0;
        if (!eatU32(p, end, &d) || p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        eng->moveHighlight(static_cast<int>(static_cast<std::int32_t>(d)));
        break;
    }
    case ImeIpcOpcode::SetHighlight: {
        std::uint32_t idx = 0;
        if (!eatU32(p, end, &idx) || p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        eng->setHighlightIndex(static_cast<int>(idx));
        break;
    }
    case ImeIpcOpcode::FeedCandidatePick: {
        std::uint32_t idx = 0;
        if (!eatU32(p, end, &idx) || p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        eng->feedCandidatePick(static_cast<size_t>(idx));
        break;
    }
    case ImeIpcOpcode::TryForwardCandidateKey: {
        std::uint32_t vk = 0;
        if (!eatU32(p, end, &vk) || p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        if (eng->tryForwardCandidateKey(vk)) {
            flags |= 1u;
        }
        break;
    }
    case ImeIpcOpcode::TryForwardPreeditCommit:
        if (p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        if (eng->tryForwardPreeditCommit()) {
            flags |= 1u;
        }
        break;
    case ImeIpcOpcode::ImManagerHotkeyWouldEat: {
        std::uint32_t vk = 0;
        std::uint64_t lp = 0;
        if (!eatU32(p, end, &vk) || !eatU64(p, end, &lp) || p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        if (eng->imManagerHotkeyWouldEat(
                vk, static_cast<std::uintptr_t>(lp))) {
            flags |= 2u;
        }
        break;
    }
    case ImeIpcOpcode::TryConsumeImManagerHotkey: {
        std::uint32_t vk = 0;
        std::uint64_t lp = 0;
        if (!eatU32(p, end, &vk) || !eatU64(p, end, &lp) || p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        if (eng->tryConsumeImManagerHotkey(
                vk, static_cast<std::uintptr_t>(lp))) {
            flags |= 1u;
        }
        break;
    }
    case ImeIpcOpcode::FcitxModifierHotkeyUsesFullKeyEvent: {
        std::uint32_t vk = 0;
        if (!eatU32(p, end, &vk) || p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        if (eng->fcitxModifierHotkeyUsesFullKeyEvent(vk)) {
            flags |= 4u;
        }
        break;
    }
    case ImeIpcOpcode::DeliverFcitxRawKeyEvent: {
        std::uint32_t vk = 0, rel = 0;
        std::uint64_t lp = 0;
        std::uint32_t hostMods = fcitx::ImeEngine::kFcitxRawKeyUseProcessKeyboardState;
        if (!eatU32(p, end, &vk) || !eatU64(p, end, &lp) ||
            !eatU32(p, end, &rel)) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        if (p != end) {
            if (static_cast<std::size_t>(end - p) < 4 ||
                !eatU32(p, end, &hostMods) || p != end) {
                return fcitx::imeIpcEncodeErrorResponse(1);
            }
        }
        if (eng->deliverFcitxRawKeyEvent(vk, static_cast<std::uintptr_t>(lp),
                                         rel != 0, hostMods)) {
            flags |= 1u;
        }
        break;
    }
    case ImeIpcOpcode::ActivateProfileInputMethod: {
        std::string name;
        if (!eatUtf8(p, end, &name) || p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        eng->activateProfileInputMethod(name);
        break;
    }
    case ImeIpcOpcode::ActivateTrayStatusAction: {
        std::string name;
        if (!eatUtf8(p, end, &name) || p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        eng->activateTrayStatusAction(name);
        break;
    }
    case ImeIpcOpcode::ReloadPinyinConfig:
        if (p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        eng->reloadPinyinConfig();
        break;
    case ImeIpcOpcode::ReloadRimeConfig:
        if (p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        eng->reloadRimeAddonConfig();
        break;
    case ImeIpcOpcode::InvokeInputMethodSubConfig: {
        std::string a, b;
        if (!eatUtf8(p, end, &a) || !eatUtf8(p, end, &b) || p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        eng->invokeInputMethodSubConfig(a, b);
        break;
    }
    case ImeIpcOpcode::ServerPopCommit:
        if (p != end) {
            return fcitx::imeIpcEncodeErrorResponse(1);
        }
        drain = true;
        break;
    default:
        return fcitx::imeIpcEncodeErrorResponse(3);
    }

    if (p != end) {
        return fcitx::imeIpcEncodeErrorResponse(2);
    }

    eng->pumpEventLoopForUi();
    return fcitx::imeIpcEncodeSuccessResponse(eng, drain, flags);
}

void serveLoop(fcitx::Fcitx5ImePipeShared *shared) {
    fcitx::tsfTrace(
        "Fcitx5ImePipeServer started (one Instance, per-connection session)");
    const std::wstring path = fcitx::imeIpcNamedPipePath();
    for (;;) {
        HANDLE h = CreateNamedPipeW(
            path.c_str(), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            static_cast<DWORD>(fcitx::kImeIpcMaxPacket),
            static_cast<DWORD>(fcitx::kImeIpcMaxPacket), 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            Sleep(500);
            continue;
        }
        const BOOL okPipe = ConnectNamedPipe(h, nullptr);
        if (!okPipe && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(h);
            continue;
        }
        auto session = std::make_unique<fcitx::Fcitx5ImeEngine>();
        if (!session->initAsPipeSession(shared)) {
            DisconnectNamedPipe(h);
            CloseHandle(h);
            continue;
        }
        ImeEngine *eng = session.get();
        for (;;) {
            fcitx::ImeIpcFrameHeader qh = {};
            if (!fcitx::imeIpcReadAll(h, &qh, sizeof(qh))) {
                break;
            }
            if (qh.magic != fcitx::kImeIpcFrameMagic ||
                qh.version != fcitx::kImeIpcVersion ||
                qh.bodySize > fcitx::kImeIpcMaxPacket) {
                break;
            }
            std::vector<std::uint8_t> body(qh.bodySize);
            if (qh.bodySize > 0 &&
                !fcitx::imeIpcReadAll(h, body.data(), qh.bodySize)) {
                break;
            }
            const auto opc = static_cast<ImeIpcOpcode>(qh.opcodeOrStatus);
            std::vector<std::uint8_t> resp =
                handleRequest(eng, opc, body.data(), body.size());
            if (!fcitx::imeIpcWriteAll(h, resp.data(), resp.size())) {
                break;
            }
        }
        session.reset();
        DisconnectNamedPipe(h);
        CloseHandle(h);
    }
}

} // namespace

int main() {
    fcitx::Fcitx5ImePipeShared shared;
    if (!shared.init()) {
        return 1;
    }
    serveLoop(&shared);
    return 0;
}
