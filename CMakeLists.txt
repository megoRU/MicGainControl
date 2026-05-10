cmake_minimum_required(VERSION 3.15)
project(MicLock CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Unicode build
add_definitions(-DUNICODE -D_UNICODE)

# Release optimizations
if(MSVC)
    add_compile_options(/W4 /WX /MT)
    add_link_options(/SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup)
endif()

# Fetch nlohmann/json
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Resources
set(RESOURCE_FILES resources.rc)

add_executable(MicLock
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

target_link_libraries(MicLock PRIVATE nlohmann_json::nlohmann_json Ole32 Shell32 User32 Advapi32 Mmdevapi)
