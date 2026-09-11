#include "pch.h"
#include <windows.h>
#include <string>
#include "MainWindow.hpp"

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

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR /*pCmdLine*/, int nCmdShow) {
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"MicGainControl_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    RegisterAutostart();

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return hr;
    }

    {
        MainWindow mainWindow(hInstance);
        if (mainWindow.Initialize(SW_HIDE)) {
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    CoUninitialize();

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return 0;
}
