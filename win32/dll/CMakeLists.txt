add_library(fcitx5-x86_64 SHARED
    main.cpp
    register.cpp
    util.cpp
)

if(NOT MSVC)
  target_compile_options(fcitx5-x86_64 PRIVATE "-Wno-dll-attribute-on-redeclaration")
endif()

target_link_libraries(fcitx5-x86_64 PRIVATE tsf ole32 oleaut32 shell32)
if(WIN32 AND NOT MSVC)
  target_link_libraries(fcitx5-x86_64 PRIVATE uuid)
endif()
# tsf (Fcitx5ImeEngine) calls uv_run; static lib does not propagate -luv to this DLL.
if(TARGET Fcitx5Core AND NOT MSVC)
  target_link_libraries(fcitx5-x86_64 PRIVATE uv)
endif()

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
  # TSF RegisterProfiles looks for penguin.ico next to the IME DLL (optional if not built).
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/penguin.ico"
          DESTINATION "${CMAKE_INSTALL_BINDIR}" OPTIONAL)

  # MinGW/Clang (e.g. MSYS2 CLANG64): libc++ and libFcitx5Utils deps are separate DLLs.
  # regsvr32 loads the IME from deploy bin — those must sit next to libfcitx5-x86_64.dll.
  if(NOT MSVC)
    get_filename_component(_IME_COMPILER_BINDIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    # libIMECore / libIMEPinyin (pinyin addon) link libzstd; must be next to IME for TSF.
    set(_IME_RUNTIME_DLLS
      libc++.dll
      libwinpthread-1.dll
      libintl-8.dll
      libiconv-2.dll
      libuv-1.dll
      libdl.dll
      libzstd.dll
    )
    set(_ime_runtime_post_cmds)
    foreach(_dll IN LISTS _IME_RUNTIME_DLLS)
      set(_src "${_IME_COMPILER_BINDIR}/${_dll}")
      if(EXISTS "${_src}")
        install(FILES "${_src}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
        list(APPEND _ime_runtime_post_cmds
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_src}" $<TARGET_FILE_DIR:fcitx5-x86_64>)
      endif()
    endforeach()
    if(_ime_runtime_post_cmds)
      add_custom_command(
        TARGET fcitx5-x86_64 POST_BUILD
        ${_ime_runtime_post_cmds}
        COMMENT "Copy MinGW/Clang runtime DLLs next to IME DLL")
    endif()
  endif()
endif()
