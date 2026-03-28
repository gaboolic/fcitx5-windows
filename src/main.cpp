#include <fcitx-utils/environ.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
#include <filesystem>
#include <windows.h>

namespace fs = std::filesystem;

#ifdef ENABLE_KEYBOARD
#include <fcitx/addoninstance.h>
FCITX_DEFINE_STATIC_ADDON_REGISTRY(getStaticAddon)
FCITX_IMPORT_ADDON_FACTORY(getStaticAddon, keyboard);
#endif

namespace fcitx {
std::unique_ptr<Instance> instance;
std::unique_ptr<fcitx::EventDispatcher> dispatcher;

void setenv(const char *name, const std::string &value) {
    setEnvironment(name, value.c_str());
}

void setupRimeUserDirEnv() {
    char existing[MAX_PATH] = {};
    if (GetEnvironmentVariableA("FCITX_RIME_USER_DIR", existing, MAX_PATH) >
        0) {
        return;
    }
    char appData[MAX_PATH] = {};
    const DWORD len = GetEnvironmentVariableA("APPDATA", appData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return;
    }
    auto rimeUserDir = ::fs::path(appData) / "Fcitx5" / "rime";
    std::error_code ec;
    ::fs::create_directories(rimeUserDir, ec);
    setenv("FCITX_RIME_USER_DIR", rimeUserDir.string());
}

void setupEnv() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    auto rootPath = ::fs::path(path).parent_path().parent_path();
    auto fcitx_addon_dirs = rootPath / "lib" / "fcitx5";
    setenv("FCITX_ADDON_DIRS", fcitx_addon_dirs.string());
    auto xdg_data_dirs = rootPath / "share";
    auto fcitx_data_dirs = xdg_data_dirs / "fcitx5";
    setenv("XDG_DATA_DIRS", xdg_data_dirs.string());
    setenv("FCITX_DATA_DIRS", fcitx_data_dirs.string());
    auto libime_models = rootPath / "lib" / "libime";
    setenv("LIBIME_MODEL_DIRS", libime_models.string());
    setupRimeUserDirEnv();
}

void start() {
    Log::setLogRule("*=5,notimedate");
    setupEnv();
    instance = std::make_unique<Instance>(0, nullptr);
    auto &addonMgr = instance->addonManager();
#ifdef ENABLE_KEYBOARD
    addonMgr.registerDefaultLoader(&getStaticAddon());
#else
    addonMgr.registerDefaultLoader(nullptr);
#endif
    instance->initialize();
    dispatcher = std::make_unique<fcitx::EventDispatcher>();
    dispatcher->attach(&instance->eventLoop());
    instance->eventLoop().exec();
}
} // namespace fcitx

int main() {
    fcitx::start();
    return 0;
}
