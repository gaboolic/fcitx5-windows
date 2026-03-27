#pragma once

// Standalone win32/ CMake (no Fcitx5Core): no fcitx-utils on include path. Keep
// FCITX_* log call sites compiling with the same macro shape as log.h.

#if FCITX_WIN32_IME_WITH_CORE
#include <fcitx-utils/log.h>
#else

namespace fcitx {

class TsfStubLogBuilder {
  public:
    TsfStubLogBuilder &self() { return *this; }
    template <typename T> TsfStubLogBuilder &operator<<(T &&) { return *this; }
};

} // namespace fcitx

#define FCITX_TSF_LOG_STUB_BODY                                                \
    for (bool fcitxLogEnabled = false; fcitxLogEnabled;                        \
         fcitxLogEnabled = false)                                              \
    ::fcitx::TsfStubLogBuilder().self()

#define FCITX_DEBUG() FCITX_TSF_LOG_STUB_BODY
#define FCITX_INFO() FCITX_TSF_LOG_STUB_BODY
#define FCITX_WARN() FCITX_TSF_LOG_STUB_BODY
#define FCITX_ERROR() FCITX_TSF_LOG_STUB_BODY

#endif
