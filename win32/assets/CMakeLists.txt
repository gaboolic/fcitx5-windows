set(PENGUIN_SVG "${CMAKE_CURRENT_SOURCE_DIR}/penguin.svg")
set(PENGUIN_ICO "${PROJECT_BINARY_DIR}/dll/penguin.ico")

# Ninja invokes cmd.exe for this step; cmd often does not inherit MSYS/PowerShell PATH, so a bare
# `magick` fails even when ImageMagick is installed. Resolve an absolute path at configure time.
if(NOT MAGICK_EXECUTABLE)
  if(WIN32)
    set(_magick_hints "")
    # Note: $ENV{ProgramFiles(x86)} is invalid CMake syntax; 64-bit ImageMagick is under ProgramW6432/PROGRAMFILES.
    foreach(_root IN ITEMS "$ENV{ProgramW6432}" "$ENV{PROGRAMFILES}")
      if(_root)
        file(GLOB _im_dirs "${_root}/ImageMagick-*")
        list(APPEND _magick_hints ${_im_dirs})
      endif()
    endforeach()
    find_program(
      MAGICK_EXECUTABLE
      NAMES magick magick.exe
      HINTS ${_magick_hints}
      DOC "ImageMagick CLI (penguin.ico)")
  endif()
  if(NOT MAGICK_EXECUTABLE)
    find_program(MAGICK_EXECUTABLE NAMES magick magick.exe)
  endif()
endif()

if(NOT MAGICK_EXECUTABLE)
  message(
    FATAL_ERROR
      "ImageMagick 'magick' not found (needed for penguin.ico). "
      "Install ImageMagick for Windows, or pass -DMAGICK_EXECUTABLE=C:/path/to/magick.exe to CMake."
  )
endif()

add_custom_command(
  OUTPUT "${PENGUIN_ICO}"
  COMMAND "${MAGICK_EXECUTABLE}" -background none "${PENGUIN_SVG}" "${PENGUIN_ICO}"
  DEPENDS "${PENGUIN_SVG}"
  VERBATIM)

# Sibling win32/dll and LangBarTray use this path (not CMAKE_CURRENT_BINARY_DIR under dll/).
set(FCITX_WIN32_PENGUIN_ICO "${PENGUIN_ICO}" PARENT_SCOPE)

add_custom_target(GenerateIco ALL
    DEPENDS "${PENGUIN_ICO}"
)
