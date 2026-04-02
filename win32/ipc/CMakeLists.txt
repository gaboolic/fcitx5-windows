add_library(fcitx5_ime_ipc STATIC
  Fcitx5ImeIpcProtocol.cpp
  Fcitx5ImeIpcCodec.cpp
)

target_include_directories(fcitx5_ime_ipc PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../tsf"
)

target_link_libraries(fcitx5_ime_ipc PUBLIC advapi32)

if(NOT MSVC)
  if(WIN32 OR CYGWIN OR MSYS OR CMAKE_SYSTEM_NAME STREQUAL "MSYS")
    target_compile_definitions(fcitx5_ime_ipc PRIVATE _WIN32_WINNT=0x0A00)
  endif()
endif()

add_executable(Fcitx5ImePipeServer
  Fcitx5ImePipeServerMain.cpp
  ../tsf/Fcitx5ImeEngine.cpp
  ../tsf/TsfInputContext.cpp
)

target_include_directories(Fcitx5ImePipeServer PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../tsf"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(Fcitx5ImePipeServer PRIVATE
  fcitx5_ime_ipc
  Fcitx5::Core
)

set_target_properties(Fcitx5ImePipeServer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT MSVC)
  target_link_libraries(Fcitx5ImePipeServer PRIVATE uv)
endif()

if(TARGET Fcitx5Core AND TARGET Fcitx5Utils)
  add_custom_command(
    TARGET Fcitx5ImePipeServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Fcitx5Core> $<TARGET_FILE_DIR:Fcitx5ImePipeServer>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Fcitx5Utils> $<TARGET_FILE_DIR:Fcitx5ImePipeServer>
    COMMENT "Copy Fcitx5Core/Fcitx5Utils next to Fcitx5ImePipeServer")
endif()

install(TARGETS Fcitx5ImePipeServer DESTINATION "${FCITX_INSTALL_BINDIR}")
