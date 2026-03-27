if(TARGET Fcitx5Core)
  add_executable(fcitx5-config-win32 WIN32 configui_main.cpp)
  target_link_libraries(fcitx5-config-win32 PRIVATE Fcitx5::Core comctl32 shell32)
  if(NOT MSVC)
    if(WIN32 OR CYGWIN OR MSYS OR CMAKE_SYSTEM_NAME STREQUAL "MSYS")
      # wWinMain + GUI subsystem
      target_link_options(fcitx5-config-win32 PRIVATE -mwindows -municode)
    endif()
  endif()
  install(TARGETS fcitx5-config-win32 RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

  if(TARGET fcitx5-x86_64)
    add_custom_command(
      TARGET fcitx5-config-win32
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:fcitx5-config-win32> $<TARGET_FILE_DIR:fcitx5-x86_64>
      COMMENT "Copy fcitx5-config-win32 next to IME DLL for portable layout")
  endif()
endif()
