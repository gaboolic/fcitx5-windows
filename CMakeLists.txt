cmake_minimum_required(VERSION 3.27)

project(fcitx5-windows VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)

option(ENABLE_TESTING_ADDONS "" OFF)
option(ENABLE_TEST "" OFF)
option(ENABLE_COVERAGE "" OFF)
option(ENABLE_ENCHANT "" OFF)
option(ENABLE_X11 "" OFF)
option(ENABLE_WAYLAND "" OFF)
option(ENABLE_DBUS "" OFF)
option(ENABLE_DOC "" OFF)
option(ENABLE_SERVER "" OFF)
option(ENABLE_KEYBOARD "" OFF)
option(USE_SYSTEMD "" OFF)
option(ENABLE_XDGAUTOSTART "" OFF)
option(ENABLE_EMOJI "" OFF)
option(ENABLE_LIBUUID "" OFF)
option(BUILD_SPELL_DICT "" ${CYGWIN})

if (CYGWIN)
    set(CMAKE_INSTALL_PREFIX /usr)
    set(ECM_DIR /clang64/share/ECM/cmake)
    add_compile_definitions(-D_GNU_SOURCE)
else()
    # Default to C: only when the user did not pass -DCMAKE_INSTALL_PREFIX. An unconditional
    # assignment would override e.g. -DCMAKE_INSTALL_PREFIX=$STAGE from build-pinyin-stage.sh,
    # breaking Fcitx5UtilsConfig.cmake (FCITX_INSTALL_CMAKECONFIG_DIR would become C:/lib/cmake).
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "C:" CACHE PATH "Install path prefix" FORCE)
    endif()
    set(ENV{PKG_CONFIG_PATH} "${CMAKE_SYSROOT}/lib/pkgconfig")
    # For std::osyncstream
    add_compile_options(-fexperimental-library)
endif()

add_subdirectory(fcitx5)
target_compile_definitions(Fcitx5Utils PUBLIC -DFcitx5Utils_EXPORTS)
target_compile_definitions(Fcitx5Core PUBLIC -DFcitx5Core_EXPORTS)

if (CYGWIN)
    # exe needs dll in the same directory.
    set(FCITX5UTILS_DLL_NAME msys-Fcitx5Utils-2.dll)
    set(FCITX5UTILS_COPY "${PROJECT_BINARY_DIR}/fcitx5/src/modules/spell/${FCITX5UTILS_DLL_NAME}")
    add_custom_command(
        OUTPUT "${FCITX5UTILS_COPY}"
        DEPENDS Fcitx5Utils
        COMMAND cp "$<TARGET_FILE:Fcitx5Utils>" "${FCITX5UTILS_COPY}"
    )
    add_custom_target(copy_fcitx5utils ALL DEPENDS "${FCITX5UTILS_COPY}")
    add_dependencies(spell_en_dict copy_fcitx5utils)
endif()

add_subdirectory(src)

option(FCITX5_WINDOWS_BUILD_WIN32_IME
       "Build win32/ TSF IME target (from repo root: links Fcitx5Core when built together)" ON)
if(FCITX5_WINDOWS_BUILD_WIN32_IME)
    add_subdirectory(win32)
endif()

# Example IME profiles: installer copies profile.pinyin.example → %AppData%\...\profile
install(
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/contrib/fcitx5/profile.pinyin.example"
        "${CMAKE_CURRENT_SOURCE_DIR}/contrib/fcitx5/profile.pinyin-only.example"
  DESTINATION share/fcitx5)
