add_library(fcitx5-x86_64 SHARED
    main.cpp
    register.cpp
    util.cpp
)

if(NOT MSVC)
  target_compile_options(fcitx5-x86_64 PRIVATE "-Wno-dll-attribute-on-redeclaration")
endif()

target_link_libraries(fcitx5-x86_64 tsf)
