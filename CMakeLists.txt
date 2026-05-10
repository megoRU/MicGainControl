cmake_minimum_required(VERSION 3.15)

project(MicGainControl CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use dynamic MSVC runtime (/MD, /MDd)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Unicode build
add_definitions(-DUNICODE -D_UNICODE)

if(MSVC)
    add_compile_options(/W4 /WX)
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

target_link_libraries(MicGainControl PRIVATE
    Ole32
    Shell32
    User32
    Advapi32
    Mmdevapi
)
