cmake_minimum_required(VERSION 3.15)

project(MicGainControl CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use dynamic MSVC runtime (/MD, /MDd)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Unicode build
add_definitions(-DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000000)

if(MSVC)
    add_compile_options(/W4 /WX /utf-8)
endif()

set(RESOURCE_FILES resources.rc)

add_executable(MicGainControl
    WIN32
    main.cpp
    ConfigManager.cpp
    ConfigManager.hpp
    AudioManager.cpp
    AudioManager.hpp
    TrayManager.cpp
    TrayManager.hpp
    resource.h
    ${RESOURCE_FILES}
)


if(MSVC)
    target_link_options(MicGainControl PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
    )
endif()

target_link_libraries(MicGainControl PRIVATE
    Ole32
    Shell32
    User32
    Advapi32
    Mmdevapi
    Comctl32
    Dwmapi
    UxTheme
)
