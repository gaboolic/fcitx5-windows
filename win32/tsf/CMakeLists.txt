add_library(tsf STATIC
    Compartment.cpp
    LangBarTray.cpp
    tsf.cpp
    CandidateListUiElement.cpp
    TextInputProcessorEx.cpp
    ThreadMgrEventSink.cpp
    TextEditSink.cpp
    KeyEventSink.cpp
    EditSession.cpp
    CompositionSink.cpp
    CandidateWindow.cpp
    StubImeEngine.cpp
)

# MSYS2 job uses /usr/bin/g++ (often CMAKE_SYSTEM_NAME=MSYS, not WIN32).
if(NOT MSVC)
    if(WIN32 OR CYGWIN OR MSYS OR CMAKE_SYSTEM_NAME STREQUAL "MSYS")
        target_sources(tsf PRIVATE MsctfMingwCompat.cpp)
        # GetDpiForWindow (Win10+); MinGW w32api defaults to older _WIN32_WINNT.
        target_compile_definitions(tsf PRIVATE _WIN32_WINNT=0x0A00)
    endif()
endif()

if(WIN32)
    target_link_libraries(tsf PUBLIC imm32)
endif()

if(FCITX_WIN32_IME_WITH_CORE)
    if(FCITX5_WINDOWS_IME_IPC AND TARGET fcitx5_ime_ipc)
        target_sources(tsf PRIVATE ../ipc/PipeImeEngine.cpp)
        target_link_libraries(tsf PUBLIC fcitx5_ime_ipc)
        # Include paths + any TSF sources that still use fcitx headers without
        # linking Fcitx5ImeEngine into the DLL.
        target_link_libraries(tsf PRIVATE Fcitx5::Core)
        target_include_directories(tsf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../ipc")
        target_compile_definitions(tsf PUBLIC FCITX5_WINDOWS_IME_IPC=1)
    else()
        target_sources(tsf PRIVATE Fcitx5ImeEngine.cpp TsfInputContext.cpp)
        target_link_libraries(tsf PUBLIC Fcitx5::Core advapi32)
        target_compile_definitions(tsf PUBLIC FCITX5_WINDOWS_IME_IPC=0)
    endif()
    target_compile_definitions(tsf PUBLIC FCITX_WIN32_IME_WITH_CORE=1)
else()
    target_compile_definitions(tsf PUBLIC FCITX_WIN32_IME_WITH_CORE=0
                                            FCITX5_WINDOWS_IME_IPC=0)
endif()
