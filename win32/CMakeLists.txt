cmake_minimum_required(VERSION 3.27)

# Default to clang (e.g. CI). Override for MSVC, e.g.:
#   cmake -B build -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
if(NOT CMAKE_CXX_COMPILER)
  set(CMAKE_C_COMPILER clang)
  set(CMAKE_CXX_COMPILER clang++)
endif()

project(fcitx5-win32 VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(assets)
add_subdirectory(tsf)
add_subdirectory(dll)

enable_testing()
add_subdirectory(tests)
