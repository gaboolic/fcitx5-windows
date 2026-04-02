#pragma once

#include "Fcitx5ImeIpcProtocol.h"
#include "ImeEngine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fcitx {

struct ImeIpcDecoded {
    std::string drainedCommitUtf8;
    std::uint32_t flags = 0;
    std::wstring preedit;
    int caretUtf16 = 0;
    int highlight = 0;
    std::vector<std::wstring> candidates;
    std::string currentImUtf8;
    std::vector<ProfileInputMethodItem> profileIms;
    std::vector<TrayStatusActionItem> trayActions;
};

std::vector<std::uint8_t> imeIpcEncodeRequest(ImeIpcOpcode op,
                                              const std::vector<std::uint8_t> &body);

/// @param drainOneCommit if true, pops one commit on the engine before snapshot.
std::vector<std::uint8_t>
imeIpcEncodeSuccessResponse(ImeEngine *engine, bool drainOneCommit,
                            std::uint32_t flags);

std::vector<std::uint8_t> imeIpcEncodeErrorResponse(std::uint32_t status);

bool imeIpcDecodeResponsePacket(const std::vector<std::uint8_t> &packet,
                                ImeIpcDecoded *out);

} // namespace fcitx
