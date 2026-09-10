#include "pch.h"
#include <windows.h>
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include "App.xaml.h"
#include <string>

#ifndef WINDOWSAPPSDK_RELEASE_MAJOR_MINOR
#define WINDOWSAPPSDK_RELEASE_MAJOR_MINOR 0x00010005u
#endif

#ifndef WINDOWSAPPSDK_RELEASE_VERSION_TAG_W
#define WINDOWSAPPSDK_RELEASE_VERSION_TAG_W L""
#endif

void RegisterAutostart() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    std::wstring quotedPath = L"\"" + std::wstring(exePath) + L"\"";

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"MicGainControl", 0, REG_SZ, reinterpret_cast<const BYTE*>(quotedPath.c_str()), static_cast<DWORD>((quotedPath.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}

int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, PWSTR /*pCmdLine*/, int /*nShowCmd*/) {
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"MicGainControl_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    RegisterAutostart();

    const HRESULT hr = MddBootstrapInitialize2(
        WINDOWSAPPSDK_RELEASE_MAJOR_MINOR,
        WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
        PACKAGE_VERSION{},
        MddBootstrapInitializeOptions_OnNoMatch_ShowUI
    );

    if (FAILED(hr)) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return hr;
    }

    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);

        winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
            winrt::make<winrt::MicGainControl::implementation::App>();
        });

        winrt::uninit_apartment();
    }

    MddBootstrapShutdown();

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return 0;
}
