#include <fcitx-utils/environ.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;

namespace {
std::string pathUtf8ForFcitxEnv(const fs::path &p) {
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

void prependProcessPath(const fs::path &dir) {
    if (dir.empty() || !fs::exists(dir)) {
        return;
    }
    const std::wstring dirWide = dir.wstring();
    const DWORD existingLen = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::wstring existing;
    if (existingLen > 0) {
        existing.resize(existingLen - 1);
        GetEnvironmentVariableW(L"PATH", existing.data(), existingLen);
    }
    const std::wstring prefix = dirWide + L";";
    if (!existing.empty()) {
        std::wstring existingLower(existing);
        std::wstring prefixLower(prefix);
        CharLowerBuffW(existingLower.data(),
                       static_cast<DWORD>(existingLower.size()));
        CharLowerBuffW(prefixLower.data(),
                       static_cast<DWORD>(prefixLower.size()));
        if (existingLower.rfind(prefixLower, 0) == 0) {
            return;
        }
    }
    SetEnvironmentVariableW(L"PATH", (prefix + existing).c_str());
}

void setupDllSearchPath(const fs::path &rootPath) {
    static std::vector<DLL_DIRECTORY_COOKIE> cookies;
    const std::vector<fs::path> dirs = {
        (rootPath / "bin").lexically_normal(),
        (rootPath / "lib").lexically_normal(),
        (rootPath / "lib" / "fcitx5").lexically_normal(),
    };

    for (const auto &dir : dirs) {
        prependProcessPath(dir);
    }

    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) {
        return;
    }
    using SetDefaultDllDirectoriesFn = BOOL(WINAPI *)(DWORD);
    using AddDllDirectoryFn = DLL_DIRECTORY_COOKIE(WINAPI *)(PCWSTR);
    auto setDefaultDllDirectories =
        reinterpret_cast<SetDefaultDllDirectoriesFn>(
            GetProcAddress(kernel32, "SetDefaultDllDirectories"));
    auto addDllDirectory = reinterpret_cast<AddDllDirectoryFn>(
        GetProcAddress(kernel32, "AddDllDirectory"));
    if (!setDefaultDllDirectories || !addDllDirectory) {
        return;
    }
    setDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
                             LOAD_LIBRARY_SEARCH_USER_DIRS);
    for (const auto &dir : dirs) {
        if (dir.empty() || !fs::exists(dir)) {
            continue;
        }
        cookies.push_back(addDllDirectory(dir.wstring().c_str()));
    }
}
} // namespace

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
    std::wstring buf(MAX_PATH, L'\0');
    const DWORD r =
        GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (r == 0) {
        return;
    }
    buf.resize(r);
    const auto rootPath =
        ::fs::path(buf).parent_path().parent_path().lexically_normal();
    setupDllSearchPath(rootPath);
    const auto fcitx_addon_dirs = rootPath / "lib" / "fcitx5";
    setenv("FCITX_ADDON_DIRS", pathUtf8ForFcitxEnv(fcitx_addon_dirs));
    const auto xdg_data_dirs = rootPath / "share";
    const auto fcitx_data_dirs = xdg_data_dirs / "fcitx5";
    setenv("XDG_DATA_DIRS", pathUtf8ForFcitxEnv(xdg_data_dirs));
    setenv("FCITX_DATA_DIRS", pathUtf8ForFcitxEnv(fcitx_data_dirs));
    const auto libime_models = rootPath / "lib" / "libime";
    setenv("LIBIME_MODEL_DIRS", pathUtf8ForFcitxEnv(libime_models));
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
