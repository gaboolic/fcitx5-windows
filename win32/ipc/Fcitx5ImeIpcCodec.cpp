#include "Fcitx5ImeIpcCodec.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>

namespace fcitx {
namespace {

void appendU32(std::vector<std::uint8_t> &b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
}

bool readU32(const std::uint8_t *&p, const std::uint8_t *end,
             std::uint32_t *out) {
    if (static_cast<std::size_t>(end - p) < 4) {
        return false;
    }
    *out = static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
    p += 4;
    return true;
}

std::string wideToUtf8(std::wstring_view w) {
    if (w.empty()) {
        return {};
    }
    const int n =
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                            nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        s.data(), n, nullptr, nullptr);
    return s;
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
        std::wstring w(static_cast<std::size_t>(wlen2), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()),
                            w.data(), wlen2);
        return w;
    }
    std::wstring w(static_cast<std::size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u8.data(),
                        static_cast<int>(u8.size()), w.data(), wlen);
    return w;
}

bool readUtf8Blob(const std::uint8_t *&p, const std::uint8_t *end,
                  std::string *out) {
    std::uint32_t len = 0;
    if (!readU32(p, end, &len) || len > 256 * 1024 ||
        static_cast<std::size_t>(end - p) < len) {
        return false;
    }
    out->assign(reinterpret_cast<const char *>(p),
                reinterpret_cast<const char *>(p + len));
    p += len;
    return true;
}

} // namespace

std::vector<std::uint8_t>
imeIpcEncodeRequest(ImeIpcOpcode op, const std::vector<std::uint8_t> &body) {
    ImeIpcFrameHeader h{};
    h.magic = kImeIpcFrameMagic;
    h.version = kImeIpcVersion;
    h.opcodeOrStatus = static_cast<std::uint32_t>(op);
    h.bodySize = static_cast<std::uint32_t>(
        std::min(body.size(), static_cast<std::size_t>(kImeIpcMaxPacket)));
    std::vector<std::uint8_t> out;
    out.resize(sizeof(h) + body.size());
    std::memcpy(out.data(), &h, sizeof(h));
    if (!body.empty()) {
        std::memcpy(out.data() + sizeof(h), body.data(), body.size());
    }
    return out;
}

std::vector<std::uint8_t> imeIpcEncodeSuccessResponse(ImeEngine *engine,
                                                      bool drainOneCommit,
                                                      std::uint32_t flags) {
    if (!engine) {
        return {};
    }
    std::vector<std::uint8_t> body;
    std::string drainedUtf8;
    if (drainOneCommit) {
        std::wstring w = engine->drainNextCommit();
        drainedUtf8 = wideToUtf8(w);
    }
    appendU32(body, drainedUtf8.empty() ? 0u : 1u);
    if (!drainedUtf8.empty()) {
        appendU32(body, static_cast<std::uint32_t>(drainedUtf8.size()));
        body.insert(body.end(), drainedUtf8.begin(), drainedUtf8.end());
    }
    appendU32(body, flags);
    const std::string preU8 = wideToUtf8(engine->preedit());
    appendU32(body, static_cast<std::uint32_t>(preU8.size()));
    body.insert(body.end(), preU8.begin(), preU8.end());
    appendU32(body, static_cast<std::uint32_t>(engine->preeditCaretUtf16()));
    appendU32(body, static_cast<std::uint32_t>(engine->highlightIndex()));
    const auto &cands = engine->candidates();
    const std::uint32_t nCand = static_cast<std::uint32_t>(
        std::min(cands.size(), static_cast<std::size_t>(256)));
    appendU32(body, nCand);
    for (std::uint32_t i = 0; i < nCand; ++i) {
        const std::string c8 = wideToUtf8(cands[static_cast<std::size_t>(i)]);
        appendU32(body, static_cast<std::uint32_t>(c8.size()));
        body.insert(body.end(), c8.begin(), c8.end());
    }
    const std::string cur = engine->currentInputMethod();
    appendU32(body, static_cast<std::uint32_t>(cur.size()));
    body.insert(body.end(), cur.begin(), cur.end());
    const auto prof = engine->profileInputMethods();
    const std::uint32_t nProf = static_cast<std::uint32_t>(
        std::min(prof.size(), static_cast<std::size_t>(64)));
    appendU32(body, nProf);
    for (std::uint32_t i = 0; i < nProf; ++i) {
        const auto &it = prof[static_cast<std::size_t>(i)];
        appendU32(body, static_cast<std::uint32_t>(it.uniqueName.size()));
        body.insert(body.end(), it.uniqueName.begin(), it.uniqueName.end());
        const std::string disp = wideToUtf8(it.displayName);
        appendU32(body, static_cast<std::uint32_t>(disp.size()));
        body.insert(body.end(), disp.begin(), disp.end());
        appendU32(body, it.isCurrent ? 1u : 0u);
    }
    const auto tray = engine->trayStatusActions();
    const std::uint32_t nTray = static_cast<std::uint32_t>(
        std::min(tray.size(), static_cast<std::size_t>(32)));
    appendU32(body, nTray);
    for (std::uint32_t i = 0; i < nTray; ++i) {
        const auto &t = tray[static_cast<std::size_t>(i)];
        appendU32(body, static_cast<std::uint32_t>(t.uniqueName.size()));
        body.insert(body.end(), t.uniqueName.begin(), t.uniqueName.end());
        const std::string d8 = wideToUtf8(t.displayName);
        appendU32(body, static_cast<std::uint32_t>(d8.size()));
        body.insert(body.end(), d8.begin(), d8.end());
        appendU32(body, t.isChecked ? 1u : 0u);
    }
    ImeIpcFrameHeader rh{};
    rh.magic = kImeIpcFrameMagic;
    rh.version = kImeIpcVersion;
    rh.opcodeOrStatus = 0;
    rh.bodySize = static_cast<std::uint32_t>(body.size());
    std::vector<std::uint8_t> packet;
    packet.resize(sizeof(rh) + body.size());
    std::memcpy(packet.data(), &rh, sizeof(rh));
    std::memcpy(packet.data() + sizeof(rh), body.data(), body.size());
    return packet;
}

std::vector<std::uint8_t> imeIpcEncodeErrorResponse(std::uint32_t status) {
    ImeIpcFrameHeader rh{};
    rh.magic = kImeIpcFrameMagic;
    rh.version = kImeIpcVersion;
    rh.opcodeOrStatus = status;
    rh.bodySize = 0;
    std::vector<std::uint8_t> packet(sizeof(rh));
    std::memcpy(packet.data(), &rh, sizeof(rh));
    return packet;
}

bool imeIpcDecodeResponsePacket(const std::vector<std::uint8_t> &packet,
                                ImeIpcDecoded *out) {
    if (packet.size() < sizeof(ImeIpcFrameHeader)) {
        return false;
    }
    ImeIpcFrameHeader h{};
    std::memcpy(&h, packet.data(), sizeof(h));
    if (h.magic != kImeIpcFrameMagic || h.version != kImeIpcVersion ||
        h.bodySize > kImeIpcMaxPacket ||
        packet.size() != sizeof(ImeIpcFrameHeader) + h.bodySize) {
        return false;
    }
    if (h.opcodeOrStatus != 0) {
        return false;
    }
    const std::uint8_t *p = packet.data() + sizeof(ImeIpcFrameHeader);
    const std::uint8_t *end = p + h.bodySize;
    std::uint32_t hasDrained = 0;
    if (!readU32(p, end, &hasDrained)) {
        return false;
    }
    if (hasDrained) {
        if (!readUtf8Blob(p, end, &out->drainedCommitUtf8)) {
            return false;
        }
    } else {
        out->drainedCommitUtf8.clear();
    }
    if (!readU32(p, end, &out->flags)) {
        return false;
    }
    std::string preU8;
    if (!readUtf8Blob(p, end, &preU8)) {
        return false;
    }
    out->preedit = utf8ToWide(preU8);
    std::uint32_t caretU = 0, hiU = 0, nCand = 0;
    if (!readU32(p, end, &caretU) || !readU32(p, end, &hiU) ||
        !readU32(p, end, &nCand) || nCand > 256) {
        return false;
    }
    out->caretUtf16 = static_cast<int>(caretU);
    out->highlight = static_cast<int>(hiU);
    out->candidates.clear();
    for (std::uint32_t i = 0; i < nCand; ++i) {
        std::string c8;
        if (!readUtf8Blob(p, end, &c8)) {
            return false;
        }
        out->candidates.push_back(utf8ToWide(c8));
    }
    if (!readUtf8Blob(p, end, &out->currentImUtf8)) {
        return false;
    }
    std::uint32_t nProf = 0;
    if (!readU32(p, end, &nProf) || nProf > 64) {
        return false;
    }
    out->profileIms.clear();
    for (std::uint32_t i = 0; i < nProf; ++i) {
        ProfileInputMethodItem item;
        if (!readUtf8Blob(p, end, &item.uniqueName)) {
            return false;
        }
        std::string disp8;
        if (!readUtf8Blob(p, end, &disp8)) {
            return false;
        }
        item.displayName = utf8ToWide(disp8);
        std::uint32_t isCur = 0;
        if (!readU32(p, end, &isCur)) {
            return false;
        }
        item.isCurrent = isCur != 0;
        out->profileIms.push_back(std::move(item));
    }
    std::uint32_t nTray = 0;
    if (!readU32(p, end, &nTray) || nTray > 32) {
        return false;
    }
    out->trayActions.clear();
    for (std::uint32_t i = 0; i < nTray; ++i) {
        TrayStatusActionItem t;
        if (!readUtf8Blob(p, end, &t.uniqueName)) {
            return false;
        }
        std::string d8;
        if (!readUtf8Blob(p, end, &d8)) {
            return false;
        }
        t.displayName = utf8ToWide(d8);
        std::uint32_t chk = 0;
        if (!readU32(p, end, &chk)) {
            return false;
        }
        t.isChecked = chk != 0;
        out->trayActions.push_back(std::move(t));
    }
    return p == end;
}

} // namespace fcitx
