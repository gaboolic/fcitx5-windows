add_library(fcitx5-x86_64 SHARED
    main.cpp
    register.cpp
    util.cpp
)

if(WIN32)
  if(NOT DEFINED FCITX_WIN32_PENGUIN_ICO)
    message(
      FATAL_ERROR
        "FCITX_WIN32_PENGUIN_ICO unset: include win32/assets before win32/dll (add_subdirectory order).")
  endif()
  file(TO_CMAKE_PATH "${FCITX_WIN32_PENGUIN_ICO}" _fcitx_penguin_ico)
  string(REPLACE "\\" "/" _fcitx_penguin_ico_slash "${_fcitx_penguin_ico}")
  set(PENGUIN_ICO_FOR_RC "\"${_fcitx_penguin_ico_slash}\"")
  get_filename_component(_fcitx_win32_dir "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
  set(_fcitx_tray_chi "${_fcitx_win32_dir}/assets/tray_chi_full_capsoff.ico")
  set(_fcitx_tray_eng "${_fcitx_win32_dir}/assets/tray_eng_full_capsoff.ico")
  if(NOT EXISTS "${_fcitx_tray_chi}" OR NOT EXISTS "${_fcitx_tray_eng}")
    message(
      FATAL_ERROR
        "Tray ICOs missing: ${_fcitx_tray_chi} and/or ${_fcitx_tray_eng}")
  endif()
  file(TO_CMAKE_PATH "${_fcitx_tray_chi}" _fcitx_tray_chi_path)
  file(TO_CMAKE_PATH "${_fcitx_tray_eng}" _fcitx_tray_eng_path)
  string(REPLACE "\\" "/" _fcitx_tray_chi_slash "${_fcitx_tray_chi_path}")
  string(REPLACE "\\" "/" _fcitx_tray_eng_slash "${_fcitx_tray_eng_path}")
  set(TRAY_CHI_ICO_FOR_RC "\"${_fcitx_tray_chi_slash}\"")
  set(TRAY_ENG_ICO_FOR_RC "\"${_fcitx_tray_eng_slash}\"")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/fcitx5-ime.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/fcitx5-ime.rc"
    @ONLY)
  target_sources(fcitx5-x86_64 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/fcitx5-ime.rc")
  add_dependencies(fcitx5-x86_64 GenerateIco)
  add_custom_command(
    TARGET fcitx5-x86_64
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FCITX_WIN32_PENGUIN_ICO}"
            $<TARGET_FILE_DIR:fcitx5-x86_64>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_fcitx_tray_chi}"
            $<TARGET_FILE_DIR:fcitx5-x86_64>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_fcitx_tray_eng}"
            $<TARGET_FILE_DIR:fcitx5-x86_64>
    COMMENT
      "Copy penguin + tray chi/eng .ico next to IME DLL (RegisterProfiles + file fallback)"
  )
endif()

if(NOT MSVC)
  target_compile_options(fcitx5-x86_64 PRIVATE "-Wno-dll-attribute-on-redeclaration")
endif()

target_link_libraries(fcitx5-x86_64 PRIVATE tsf ole32 oleaut32 shell32)
if(NOT MSVC)
  if(WIN32 OR CYGWIN OR MSYS OR CMAKE_SYSTEM_NAME STREQUAL "MSYS")
    target_link_libraries(fcitx5-x86_64 PRIVATE uuid)
  endif()
endif()
# In-process Fcitx5ImeEngine calls uv_run; pipe mode does not load Core in the DLL.
if(TARGET Fcitx5Core AND NOT MSVC AND NOT FCITX5_WINDOWS_IME_IPC)
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
  install(FILES "${FCITX_WIN32_PENGUIN_ICO}" "${_fcitx_tray_chi}"
                "${_fcitx_tray_eng}"
          DESTINATION "${CMAKE_INSTALL_BINDIR}" OPTIONAL)

  # MinGW/Clang (e.g. MSYS2 CLANG64): libc++ and libFcitx5Utils deps are separate DLLs.
  # regsvr32 loads the IME from deploy bin — those must sit next to libfcitx5-x86_64.dll.
  if(NOT MSVC)
    # Cross-build: driver may live under clang64 while sysroot is clangarm64;
    # runtime DLLs must match the target (ARM64), not the host compiler dir.
    if(CMAKE_SYSROOT AND IS_DIRECTORY "${CMAKE_SYSROOT}/bin")
      set(_IME_COMPILER_BINDIR "${CMAKE_SYSROOT}/bin")
    else()
      get_filename_component(_IME_COMPILER_BINDIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    endif()
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
