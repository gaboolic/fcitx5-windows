add_library(fcitx5-x86_64 SHARED
    main.cpp
    register.cpp
    util.cpp
)

if(NOT MSVC)
  target_compile_options(fcitx5-x86_64 PRIVATE "-Wno-dll-attribute-on-redeclaration")
endif()

target_link_libraries(fcitx5-x86_64 PRIVATE tsf ole32 oleaut32)

# When built from repo root with Fcitx5Core, stage runtime DLLs next to the IME
# (same directory as Fcitx5.exe portable layout expects under bin/).
if(TARGET Fcitx5Core AND TARGET Fcitx5Utils)
  add_custom_command(
    TARGET fcitx5-x86_64
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Fcitx5Core> $<TARGET_FILE_DIR:fcitx5-x86_64>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Fcitx5Utils> $<TARGET_FILE_DIR:fcitx5-x86_64>
    COMMENT "Copy Fcitx5Core/Fcitx5Utils next to IME DLL")

  install(TARGETS fcitx5-x86_64 RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(
    FILES $<TARGET_FILE:Fcitx5Core> $<TARGET_FILE:Fcitx5Utils>
    DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
