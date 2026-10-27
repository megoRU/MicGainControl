cmake_minimum_required(VERSION 3.15)

project(MicGainControl VERSION 0.1.9 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

add_executable(MicGainControl WIN32
    src/main.cpp
    src/ConfigManager.cpp
    src/AudioManager.cpp
    src/TrayManager.cpp
    include/ConfigManager.hpp
    include/AudioManager.hpp
    include/TrayManager.hpp
    include/resource.h
    resources/resources.rc
)

target_include_directories(MicGainControl PRIVATE include)
target_compile_definitions(MicGainControl PRIVATE
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000000
)

if(MSVC)
    target_compile_options(MicGainControl PRIVATE /W4 /WX /utf-8)
    target_link_options(MicGainControl PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest"
    )
endif()

target_link_libraries(MicGainControl PRIVATE
    Ole32
    Uuid
    Shell32
    User32
    Advapi32
    Mmdevapi
    Comctl32
    Dwmapi
    UxTheme
    Gdiplus
)
