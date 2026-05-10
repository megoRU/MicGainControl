cmake_minimum_required(VERSION 3.15)
project(MicGainControl CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Unicode build
add_definitions(-DUNICODE -D_UNICODE)

# Release optimizations
if(MSVC)
    add_compile_options(/W4 /WX /MT)
    add_link_options(/SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup)
endif()

# Resources
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

target_link_libraries(MicGainControl PRIVATE Ole32 Shell32 User32 Advapi32 Mmdevapi Comctl32)
