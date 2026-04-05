cmake_minimum_required(VERSION 3.27)

# Default to clang (e.g. CI). Override for MSVC, e.g.:
#   cmake -B build -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
  endif()
  project(fcitx5-win32 VERSION 0.1.0)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  set(CMAKE_CXX_STANDARD 20)
endif()

if(TARGET Fcitx5Core)
  set(FCITX_WIN32_IME_WITH_CORE ON)
else()
  set(FCITX_WIN32_IME_WITH_CORE OFF)
endif()

option(FCITX5_WINDOWS_IME_IPC
       "TSF uses named pipe to Fcitx5ImePipeServer (out-of-process core)" OFF)
if(FCITX5_WINDOWS_IME_IPC AND FCITX_WIN32_IME_WITH_CORE)
  add_subdirectory(ipc)
endif()

add_subdirectory(assets)
add_subdirectory(tsf)
add_subdirectory(dll)
add_subdirectory(configui)

enable_testing()
add_subdirectory(tests)
