add_executable(Fcitx5 main.cpp)
# Match fcitx5's CMAKE_RUNTIME_OUTPUT_DIRECTORY and Windows standardpaths_p_win.cpp:
# exe must live under .../bin/ so portable addondir (lib/fcitx5) resolves correctly.
set_target_properties(Fcitx5 PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(Fcitx5 Fcitx5::Core)
if (ENABLE_KEYBOARD)
    target_link_libraries(Fcitx5 keyboard)
    target_compile_definitions(Fcitx5 PRIVATE ENABLE_KEYBOARD)
endif ()

install(TARGETS Fcitx5 DESTINATION "${FCITX_INSTALL_BINDIR}")
