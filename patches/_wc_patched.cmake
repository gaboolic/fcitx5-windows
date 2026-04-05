set(CMAKE_SYSTEM_NAME Windows)

if(NOT DEFINED ARCH)
 set(ARCH "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()

set(AMD64_SYSROOT "C:/msys64/clang64")
set(ARM64_SYSROOT "C:/msys64/clangarm64")

if (CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "AMD64")
 set(HOST_SYSROOT "${AMD64_SYSROOT}")
elseif (CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "ARM64")
 set(HOST_SYSROOT "${ARM64_SYSROOT}")
else()
 message(FATAL_ERROR "Unsupported host architecture '${CMAKE_HOST_SYSTEM_PROCESSOR}'.")
endif()
# For using host Ninja.
set(CMAKE_PREFIX_PATH "${HOST_SYSROOT}")

if (ARCH STREQUAL "AMD64")
 set(CMAKE_SYSROOT "${AMD64_SYSROOT}")
 set(TARGET_TRIPLET "x86_64-w64-windows-gnu")
elseif (ARCH STREQUAL "ARM64")
 set(CMAKE_SYSROOT "${ARM64_SYSROOT}")
 set(TARGET_TRIPLET "aarch64-w64-windows-gnu")
else()
 message(FATAL_ERROR "ARCH must be either 'AMD64' or 'ARM64', but got '${ARCH}'.")
endif()

if (NOT ARCH STREQUAL CMAKE_SYSTEM_PROCESSOR)
 set(CMAKE_CROSSCOMPILING TRUE)
 list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SYSROOT}")
endif()

# This assumes clang64 and clangarm64 provide same major version of clang.
execute_process(
 COMMAND "${HOST_SYSROOT}/bin/clang.exe" --version
 OUTPUT_VARIABLE CLANG_VERSION_OUTPUT
 OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX MATCH "[0-9]+" CLANG_MAJOR_VERSION "${CLANG_VERSION_OUTPUT}")
set(C_CXX_FLAGS_INIT "-resource-dir ${CMAKE_SYSROOT}/lib/clang/${CLANG_MAJOR_VERSION}")

# Avoid find_package emits -isystem which is unnecessary and puts C include dir before C++.
set(CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES "${CMAKE_SYSROOT}/include")
set(CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "${CMAKE_SYSROOT}/include;${CMAKE_SYSROOT}/include/c++/v1")

set(CMAKE_C_FLAGS_INIT "${C_CXX_FLAGS_INIT}")
set(CMAKE_CXX_FLAGS_INIT "${C_CXX_FLAGS_INIT}")
set(CMAKE_C_COMPILER_TARGET "${TARGET_TRIPLET}")
set(CMAKE_CXX_COMPILER_TARGET "${TARGET_TRIPLET}")

# Resource compiler: COFF must match the link target. Native build: use
# sysroot windres. Cross ARM64 on x86_64 host: clangarm64 windres.exe is ARM64
# and cannot run on GitHub runners — use host llvm-windres + target triplet.
if (ARCH STREQUAL "ARM64"
    AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "AMD64"
    AND EXISTS "${HOST_SYSROOT}/bin/llvm-windres.exe")
 set(CMAKE_RC_COMPILER "${HOST_SYSROOT}/bin/llvm-windres.exe" CACHE FILEPATH
                                                                "llvm-windres (host)")
 set(CMAKE_RC_COMPILER_TARGET "aarch64-w64-windows-gnu" CACHE STRING
                                                               "RC for ARM64 PE")
 # CMake often still feeds windres-style rules without --target; lld then sees
 # machine type x64 in the .res/.obj. Force ARM64 for llvm-windres explicitly.
 set(CMAKE_RC_FLAGS "--target=aarch64-w64-windows-gnu" CACHE STRING
                                                     "llvm-windres ARM64 COFF")
elseif (EXISTS "${CMAKE_SYSROOT}/bin/windres.exe")
 set(CMAKE_RC_COMPILER "${CMAKE_SYSROOT}/bin/windres.exe" CACHE FILEPATH
                                                                "windres for target triplet")
endif()

# Bypass check since it doesn't use the flags we set here.
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Reproducible.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--no-insert-timestamp")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-Wl,--no-insert-timestamp")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-Wl,--no-insert-timestamp")

# Without it there is more diff between build/{x86_64,arm64}.
set(CMAKE_SIZEOF_VOID_P 8)

set(ECM_DIR "${CMAKE_SYSROOT}/share/ECM/cmake")
set(GETTEXT_MSGMERGE_EXECUTABLE "${HOST_SYSROOT}/bin/msgmerge.exe")
set(GETTEXT_MSGFMT_EXECUTABLE "${HOST_SYSROOT}/bin/msgfmt.exe")

# Do a no-op access on the CMAKE_TOOLCHAIN_FILE variable so that CMake will not
# issue a warning on it being unused.
if (CMAKE_TOOLCHAIN_FILE)
endif()
