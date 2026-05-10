#include <windows.h>
#include <shellapi.h>
#include "ConfigManager.hpp"
#include "AudioManager.hpp"
#include "TrayManager.hpp"
#include <memory>
#include <filesystem>

namespace fs = std::filesystem;

class Application {
public:
    Application(HINSTANCE hInstance) : m_trayManager(hInstance) {}

    bool Initialize() {
        if (!m_configManager.Load()) return false;
        if (!m_audioManager.Initialize()) return false;

        m_audioManager.SetTargetVolume(m_configManager.GetConfig().microphoneVolume);
        m_audioManager.SetEnabled(m_configManager.GetConfig().enabled);
        m_trayManager.SetEnabledState(m_configManager.GetConfig().enabled);

        m_configManager.SetCallback([this](const Config& cfg) {
            m_audioManager.SetTargetVolume(cfg.microphoneVolume);
            m_audioManager.SetEnabled(cfg.enabled);
            m_trayManager.SetEnabledState(cfg.enabled);
        });
        m_configManager.StartWatching();

        m_trayManager.SetOnToggle([this]() {
            bool newState = !m_configManager.GetConfig().enabled;
            m_configManager.SetEnabled(newState);
            // The watcher will trigger OnFileChanged -> callback -> audio/tray update
        });

        m_trayManager.SetOnOpenConfig([this]() {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            std::wstring configPath = fs::path(exePath).parent_path() / L"config.json";
            ShellExecuteW(NULL, L"open", L"notepad.exe", configPath.c_str(), NULL, SW_SHOW);
        });

        m_trayManager.SetOnExit([this]() {
            PostQuitMessage(0);
        });

        return CreateHiddenWindow();
    }

    void Run() {
        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

private:
    bool CreateHiddenWindow() {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"MicLockHiddenWindow";
        RegisterClassExW(&wc);

        m_hWnd = CreateWindowExW(0, wc.lpszClassName, L"MicLock", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, this);
        if (!m_hWnd) return false;

        return m_trayManager.CreateTrayIcon(m_hWnd);
    }

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        Application* app = nullptr;
        if (uMsg == WM_NCCREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            app = (Application*)cs->lpCreateParams;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)app);
        } else {
            app = (Application*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        }

        if (app) {
            if (uMsg == WM_TRAY_ICON) {
                if (LOWORD(lParam) == WM_RBUTTONUP) {
                    app->m_trayManager.ShowContextMenu(hWnd);
                }
                return 0;
            }
        }
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    HWND m_hWnd = NULL;
    ConfigManager m_configManager;
    AudioManager m_audioManager;
    TrayManager m_trayManager;
};

void RegisterAutostart() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    std::wstring quotedPath = L"\"" + std::wstring(exePath) + L"\"";

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"MicLock", 0, REG_SZ, (BYTE*)quotedPath.c_str(), (DWORD)(quotedPath.length() + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, [[maybe_unused]] HINSTANCE hPrevInstance, [[maybe_unused]] LPSTR lpCmdLine, [[maybe_unused]] int nShowCmd) {
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"MicLock_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
        return 0;
    }

    RegisterAutostart();

    {
        Application app(hInstance);
        if (app.Initialize()) {
            app.Run();
        }
    }

    CoUninitialize();
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);

    return 0;
}
