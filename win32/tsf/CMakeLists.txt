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

if(WIN32 AND NOT MSVC)
    target_sources(tsf PRIVATE MsctfMingwCompat.cpp)
endif()

if(WIN32)
    target_link_libraries(tsf PUBLIC imm32)
endif()

if(FCITX_WIN32_IME_WITH_CORE)
    target_sources(tsf PRIVATE Fcitx5ImeEngine.cpp TsfInputContext.cpp)
    target_link_libraries(tsf PUBLIC Fcitx5::Core)
    target_compile_definitions(tsf PUBLIC FCITX_WIN32_IME_WITH_CORE=1)
else()
    target_compile_definitions(tsf PUBLIC FCITX_WIN32_IME_WITH_CORE=0)
endif()
