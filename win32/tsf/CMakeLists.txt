add_library(tsf STATIC
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
    target_sources(tsf PRIVATE Fcitx5ImeEngine.cpp TsfInputContext.cpp)
    target_link_libraries(tsf PUBLIC Fcitx5::Core advapi32)
    target_compile_definitions(tsf PUBLIC FCITX_WIN32_IME_WITH_CORE=1)
else()
    target_compile_definitions(tsf PUBLIC FCITX_WIN32_IME_WITH_CORE=0)
endif()
