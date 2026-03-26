add_executable(fcitx5-tray-helper WIN32 trayhelper_main.cpp)
target_link_libraries(fcitx5-tray-helper PRIVATE shell32)
if(WIN32 AND NOT MSVC)
  target_link_options(fcitx5-tray-helper PRIVATE -mwindows -municode)
endif()
install(TARGETS fcitx5-tray-helper RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(TARGET fcitx5-x86_64)
  add_custom_command(
    TARGET fcitx5-tray-helper
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:fcitx5-tray-helper> $<TARGET_FILE_DIR:fcitx5-x86_64>
    COMMENT "Copy fcitx5-tray-helper next to IME DLL for portable layout")
endif()
