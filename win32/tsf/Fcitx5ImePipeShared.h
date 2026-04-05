#pragma once

#include <fcitx/instance.h>

#include <memory>

namespace fcitx {

/// One shared fcitx5 `Instance` for Fcitx5ImePipeServer; each pipe connection
/// uses a separate `Fcitx5ImeEngine` / `TsfInputContext` bound to this core.
class Fcitx5ImePipeShared {
  public:
    Fcitx5ImePipeShared() = default;
    ~Fcitx5ImePipeShared() = default;

    /// Creates and initializes `Instance` (same bootstrap as in-process TSF).
    bool init();

    Instance *instance() { return instance_.get(); }
    const Instance *instance() const { return instance_.get(); }

  private:
    std::unique_ptr<Instance> instance_;
};

} // namespace fcitx
