#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fcitx {

enum class ImeIpcOpcode : std::uint32_t {
    Ping = 0,
    SyncInputPanel = 1,
    Clear = 2,
    AppendLatin = 3,
    Backspace = 4,
    MoveHighlight = 5,
    SetHighlight = 6,
    FeedCandidatePick = 7,
    TryForwardCandidateKey = 8,
    TryForwardPreeditCommit = 9,
    ImManagerHotkeyWouldEat = 10,
    TryConsumeImManagerHotkey = 11,
    FcitxModifierHotkeyUsesFullKeyEvent = 12,
    DeliverFcitxRawKeyEvent = 13,
    ActivateProfileInputMethod = 14,
    ActivateTrayStatusAction = 15,
    ReloadPinyinConfig = 16,
    InvokeInputMethodSubConfig = 17,
    ServerPopCommit = 18,
    ReloadRimeConfig = 19,
};

constexpr std::uint32_t kImeIpcFrameMagic = 0x31435446u; // "FCT1" little-endian on LE machine
constexpr std::uint32_t kImeIpcVersion = 1;
constexpr std::size_t kImeIpcMaxPacket = 512 * 1024;
constexpr wchar_t kImeIpcPipeBaseName[] = L"Fcitx5ImePipe_v1";

struct ImeIpcFrameHeader {
    std::uint32_t magic = kImeIpcFrameMagic;
    std::uint32_t version = kImeIpcVersion;
    std::uint32_t opcodeOrStatus = 0;
    std::uint32_t bodySize = 0;
};

std::wstring imeIpcNamedPipePath();

/// Only Fcitx5ImePipeServer.exe uses this; second instance exits immediately.
std::wstring imeIpcPipeServerSingletonMutexName();
/// PipeImeEngine uses this to serialize CreateProcess across host processes.
std::wstring imeIpcPipeServerLaunchMutexName();

bool imeIpcReadAll(HANDLE h, void *buf, size_t size);

bool imeIpcWriteAll(HANDLE h, const void *buf, size_t size);

} // namespace fcitx
