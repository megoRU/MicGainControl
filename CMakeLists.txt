cmake_minimum_required(VERSION 3.15)

project(MicGainControl CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Unicode build
add_definitions(-DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000000 -DWIN32_LEAN_AND_MEAN)

if(MSVC)
    add_compile_options(/W4 /WX /utf-8 /await /permissive- /bigobj)
endif()

# WinUI 3 XAML, IDL, and C++ source files
set(IDL_FILES
    App.idl
    MainWindow.idl
)

set(XAML_FILES
    App.xaml
    MainWindow.xaml
)

set(SOURCE_FILES
    main.cpp
    App.xaml.cpp
    App.xaml.h
    MainWindow.xaml.cpp
    MainWindow.xaml.h
    ConfigManager.cpp
    ConfigManager.hpp
    AudioManager.cpp
    AudioManager.hpp
    TrayManager.cpp
    TrayManager.hpp
    pch.h
    resource.h
    resources.rc
)

set_source_files_properties(App.xaml PROPERTIES VS_XAML_TYPE "ApplicationDefinition")
set_source_files_properties(MainWindow.xaml PROPERTIES VS_XAML_TYPE "Page")

add_executable(MicGainControl WIN32
    ${SOURCE_FILES}
    ${IDL_FILES}
    ${XAML_FILES}
)

target_include_directories(MicGainControl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/Generated\ Files
)

# Set MSVC target properties for Windows App SDK and C++/WinRT NuGet packages
set(CPPWINRT_VER "2.0.240111.5")
set(WASDK_VER "1.5.240311000")

set_target_properties(MicGainControl PROPERTIES
    VS_GLOBAL_KEYWORD "Win32Proj"
    VS_GLOBAL_EnableTypeInfoReflection "false"
    VS_GLOBAL_CppWinRTOptimize "true"
    VS_GLOBAL_WindowsPackageType "None"
    VS_GLOBAL_AppxPackage "false"
)

if(MSVC)
    target_link_options(MicGainControl PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
    )

    # Attach props and targets for NuGet packages if present in build tree
    set(PKG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/packages")
    if(EXISTS "${PKG_DIR}/Microsoft.Windows.CppWinRT.${CPPWINRT_VER}/build/native/Microsoft.Windows.CppWinRT.props")
        set_property(TARGET MicGainControl APPEND PROPERTY VS_PROJECT_IMPORT_BEFORE
            "${PKG_DIR}/Microsoft.Windows.CppWinRT.${CPPWINRT_VER}/build/native/Microsoft.Windows.CppWinRT.props"
            "${PKG_DIR}/Microsoft.WindowsAppSDK.${WASDK_VER}/build/native/Microsoft.WindowsAppSDK.props"
        )
        set_property(TARGET MicGainControl APPEND PROPERTY VS_PROJECT_IMPORT
            "${PKG_DIR}/Microsoft.Windows.CppWinRT.${CPPWINRT_VER}/build/native/Microsoft.Windows.CppWinRT.targets"
            "${PKG_DIR}/Microsoft.WindowsAppSDK.${WASDK_VER}/build/native/Microsoft.WindowsAppSDK.targets"
        )
    endif()
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
