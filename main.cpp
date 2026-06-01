#include <windows.h>
#include <commctrl.h>
#include "ConfigManager.hpp"
#include "AudioManager.hpp"
#include "TrayManager.hpp"
#include "resource.h"
#include <string>

namespace {
constexpr int kWindowClientWidth = 420;
constexpr int kWindowClientHeight = 180;
constexpr int kIconControlId = 1001;
constexpr int kVolumeTrackbarId = 1002;
constexpr int kVolumeValueLabelId = 1003;
constexpr int kDescriptionLabelId = 1004;
constexpr UINT kApplyExternalConfigMessage = WM_APP + 1;

std::wstring MakeVolumeLabelText(float volume) {
    int volumePercent = static_cast<int>((volume * 100.0f) + 0.5f);
    if (volumePercent < 0) {
        volumePercent = 0;
    }
    if (volumePercent > 100) {
        volumePercent = 100;
    }

    return L"Microphone volume: " + std::to_wstring(volumePercent) + L"%";
}


HMENU ControlIdToMenuHandle(int controlId) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId));
}

HBRUSH SystemColorToBrush(int systemColor) {
    return reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(systemColor + 1));
}

int VolumeToTrackbarPosition(float volume) {
    int volumePercent = static_cast<int>((volume * 100.0f) + 0.5f);
    if (volumePercent < 0) {
        volumePercent = 0;
    }
    if (volumePercent > 100) {
        volumePercent = 100;
    }

    return volumePercent;
}
}

class Application {
public:
    Application(HINSTANCE hInstance) : m_hInstance(hInstance), m_trayManager(hInstance) {}

    bool Initialize() {
        if (!m_configManager.Load()) return false;
        if (!m_audioManager.Initialize()) return false;

        ApplyConfig(m_configManager.GetConfig(), false);

        m_configManager.SetCallback([this](const Config& cfg) {
            ApplyConfig(cfg, true);
        });
        m_configManager.StartWatching();

        m_trayManager.SetOnToggle([this]() {
            bool newState = !m_configManager.GetConfig().enabled;
            m_configManager.SetEnabled(newState);
            ApplyConfig(m_configManager.GetConfig(), true);
        });

        m_trayManager.SetOnOpenConfig([this]() {
            ShowMainWindow();
        });

        m_trayManager.SetOnExit([this]() {
            m_allowExit = true;
            DestroyWindow(m_hWnd);
        });

        if (!CreateMainWindow()) {
            return false;
        }

        return m_trayManager.CreateTrayIcon(m_hWnd);
    }

    void Run() {
        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

private:
    bool CreateMainWindow() {
        INITCOMMONCONTROLSEX commonControls = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&commonControls);

        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = m_hInstance;
        wc.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
        wc.hIconSm = static_cast<HICON>(LoadImageW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = SystemColorToBrush(COLOR_WINDOW);
        wc.lpszClassName = L"MicGainControlMainWindow";

        if (!RegisterClassExW(&wc)) {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                return false;
            }
        }

        DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        DWORD windowExStyle = WS_EX_APPWINDOW;
        RECT windowRectangle = { 0, 0, kWindowClientWidth, kWindowClientHeight };
        AdjustWindowRectEx(&windowRectangle, windowStyle, FALSE, windowExStyle);

        int windowWidth = windowRectangle.right - windowRectangle.left;
        int windowHeight = windowRectangle.bottom - windowRectangle.top;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int x = (screenWidth - windowWidth) / 2;
        int y = (screenHeight - windowHeight) / 2;

        m_hWnd = CreateWindowExW(
            windowExStyle,
            wc.lpszClassName,
            L"MicGainControl",
            windowStyle,
            x,
            y,
            windowWidth,
            windowHeight,
            NULL,
            NULL,
            m_hInstance,
            this
        );

        if (!m_hWnd) {
            return false;
        }

        ShowWindow(m_hWnd, SW_SHOWNORMAL);
        UpdateWindow(m_hWnd);
        return true;
    }

    void CreateChildControls(HWND hWnd) {
        m_hLogoIcon = static_cast<HICON>(LoadImageW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
        m_hIconControl = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_ICON, 18, 18, 34, 34, hWnd, ControlIdToMenuHandle(kIconControlId), m_hInstance, NULL);
        if (m_hIconControl && m_hLogoIcon) {
            SendMessageW(m_hIconControl, STM_SETICON, reinterpret_cast<WPARAM>(m_hLogoIcon), 0);
        }

        m_hDescriptionLabel = CreateWindowExW(0, L"STATIC", L"Adjust the default microphone input volume. Changes are applied immediately.", WS_CHILD | WS_VISIBLE, 64, 18, 330, 36, hWnd, ControlIdToMenuHandle(kDescriptionLabelId), m_hInstance, NULL);

        m_hVolumeValueLabel = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 24, 70, 220, 22, hWnd, ControlIdToMenuHandle(kVolumeValueLabelId), m_hInstance, NULL);

        m_hTrackbar = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_TOOLTIPS, 20, 98, 380, 45, hWnd, ControlIdToMenuHandle(kVolumeTrackbarId), m_hInstance, NULL);
        if (m_hTrackbar) {
            SendMessageW(m_hTrackbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(m_hTrackbar, TBM_SETTICFREQ, 10, 0);
            SendMessageW(m_hTrackbar, TBM_SETPAGESIZE, 0, 10);
            SendMessageW(m_hTrackbar, TBM_SETLINESIZE, 0, 1);
        }

        HFONT dialogFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessageW(m_hDescriptionLabel, WM_SETFONT, reinterpret_cast<WPARAM>(dialogFont), TRUE);
        SendMessageW(m_hVolumeValueLabel, WM_SETFONT, reinterpret_cast<WPARAM>(dialogFont), TRUE);
        SendMessageW(m_hTrackbar, WM_SETFONT, reinterpret_cast<WPARAM>(dialogFont), TRUE);

        UpdateTrackbarFromConfig();
    }

    void ApplyConfig(const Config& cfg, bool updateWindowControls) {
        m_audioManager.SetTargetVolume(cfg.microphoneVolume);
        m_audioManager.SetEnabled(cfg.enabled);
        m_trayManager.SetEnabledState(cfg.enabled);

        if (updateWindowControls && m_hWnd) {
            PostMessageW(m_hWnd, kApplyExternalConfigMessage, 0, 0);
        }
    }

    void UpdateTrackbarFromConfig() {
        int trackbarPosition = VolumeToTrackbarPosition(m_configManager.GetConfig().microphoneVolume);
        if (m_hTrackbar) {
            SendMessageW(m_hTrackbar, TBM_SETPOS, TRUE, trackbarPosition);
        }
        UpdateVolumeLabel(trackbarPosition);
    }

    void UpdateVolumeLabel(int volumePercent) {
        float volume = static_cast<float>(volumePercent) / 100.0f;
        std::wstring labelText = MakeVolumeLabelText(volume);
        SetWindowTextW(m_hVolumeValueLabel, labelText.c_str());
    }

    void HandleTrackbarChanged() {
        if (!m_hTrackbar) {
            return;
        }

        int volumePercent = static_cast<int>(SendMessageW(m_hTrackbar, TBM_GETPOS, 0, 0));
        if (volumePercent < 0) {
            volumePercent = 0;
        }
        if (volumePercent > 100) {
            volumePercent = 100;
        }

        float volume = static_cast<float>(volumePercent) / 100.0f;
        m_configManager.SetMicrophoneVolume(volume);
        m_audioManager.ApplyVolumeImmediately(volume);
        UpdateVolumeLabel(volumePercent);
    }

    void ShowMainWindow() {
        if (!m_hWnd) {
            return;
        }

        ShowWindow(m_hWnd, SW_SHOWNORMAL);
        SetForegroundWindow(m_hWnd);
    }

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        Application* app = nullptr;
        if (uMsg == WM_NCCREATE) {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            app = reinterpret_cast<Application*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            app = reinterpret_cast<Application*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        }

        if (app) {
            switch (uMsg) {
            case WM_CREATE:
                app->CreateChildControls(hWnd);
                return 0;
            case WM_TRAY_ICON:
                if (LOWORD(lParam) == WM_RBUTTONUP) {
                    app->m_trayManager.ShowContextMenu(hWnd);
                } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
                    app->ShowMainWindow();
                }
                return 0;
            case WM_HSCROLL:
                if (reinterpret_cast<HWND>(lParam) == app->m_hTrackbar) {
                    app->HandleTrackbarChanged();
                    return 0;
                }
                break;
            case kApplyExternalConfigMessage:
                app->UpdateTrackbarFromConfig();
                return 0;
            case WM_CLOSE:
                if (app->m_allowExit) {
                    DestroyWindow(hWnd);
                } else {
                    ShowWindow(hWnd, SW_HIDE);
                }
                return 0;
            case WM_DESTROY:
                if (app->m_hLogoIcon) {
                    DestroyIcon(app->m_hLogoIcon);
                    app->m_hLogoIcon = NULL;
                }
                if (app->m_allowExit) {
                    PostQuitMessage(0);
                }
                return 0;
            }
        }
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    HINSTANCE m_hInstance = NULL;
    HWND m_hWnd = NULL;
    HWND m_hIconControl = NULL;
    HWND m_hDescriptionLabel = NULL;
    HWND m_hVolumeValueLabel = NULL;
    HWND m_hTrackbar = NULL;
    HICON m_hLogoIcon = NULL;
    bool m_allowExit = false;
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
        RegSetValueExW(hKey, L"MicGainControl", 0, REG_SZ, reinterpret_cast<const BYTE*>(quotedPath.c_str()), static_cast<DWORD>((quotedPath.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, [[maybe_unused]] HINSTANCE hPrevInstance, [[maybe_unused]] LPSTR lpCmdLine, [[maybe_unused]] int nShowCmd) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"MicGainControl_SingleInstance_Mutex");
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
