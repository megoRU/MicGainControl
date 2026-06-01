#include <windows.h>
#include <commctrl.h>
#include "ConfigManager.hpp"
#include "AudioManager.hpp"
#include "TrayManager.hpp"
#include "resource.h"
#include <string>

#ifndef TBS_TRANSPARENTBKGND
#define TBS_TRANSPARENTBKGND 0x1000
#endif

#ifndef TBCD_TICS
#define TBCD_TICS 0x0001
#endif

#ifndef TBCD_THUMB
#define TBCD_THUMB 0x0002
#endif

#ifndef TBCD_CHANNEL
#define TBCD_CHANNEL 0x0003
#endif

namespace {
constexpr int kWindowClientWidth = 420;
constexpr int kWindowClientHeight = 190;
constexpr int kVolumeTrackbarId = 1002;
constexpr int kVolumeValueLabelId = 1003;
constexpr int kDescriptionLabelId = 1004;
constexpr int kVersionLabelId = 1005;
constexpr COLORREF kAccentColor = RGB(0, 120, 215);
constexpr COLORREF kTrackBackgroundColor = RGB(225, 229, 235);
constexpr COLORREF kWindowBackgroundColor = RGB(255, 255, 255);
constexpr COLORREF kTextColor = RGB(32, 32, 32);
constexpr UINT kApplyExternalConfigMessage = WM_APP + 1;

std::wstring MakeVolumeLabelText(float volume) {
    int volumePercent = static_cast<int>((volume * 100.0f) + 0.5f);
    if (volumePercent < 0) {
        volumePercent = 0;
    }
    if (volumePercent > 100) {
        volumePercent = 100;
    }

    return L"Громкость микрофона: " + std::to_wstring(volumePercent) + L"%";
}

HMENU ControlIdToMenuHandle(int controlId) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId));
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
    Application(HINSTANCE hInstance) : m_hInstance(hInstance), m_hBackgroundBrush(CreateSolidBrush(kWindowBackgroundColor)), m_trayManager(hInstance) {}

    ~Application() {
        DestroyWindowIcons();
        if (m_hBackgroundBrush) {
            DeleteObject(m_hBackgroundBrush);
            m_hBackgroundBrush = NULL;
        }
    }

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
    int GetSystemMetricForDpi(int metric, UINT dpi) const {
        return GetSystemMetricsForDpi(metric, dpi);
    }

    HICON LoadApplicationIcon(int width, int height, bool useSharedHandle) const {
        UINT loadFlags = LR_DEFAULTCOLOR;
        if (useSharedHandle) {
            loadFlags = loadFlags | LR_SHARED;
        }

        return static_cast<HICON>(LoadImageW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, width, height, loadFlags));
    }

    void DestroyWindowIcons() {
        if (m_hSmallIcon) {
            DestroyIcon(m_hSmallIcon);
            m_hSmallIcon = NULL;
        }

        if (m_hBigIcon) {
            DestroyIcon(m_hBigIcon);
            m_hBigIcon = NULL;
        }
    }

    void UpdateWindowIcons() {
        DestroyWindowIcons();

        UINT windowDpi = GetDpiForWindow(m_hWnd);
        int smallIconWidth = GetSystemMetricForDpi(SM_CXSMICON, windowDpi);
        int smallIconHeight = GetSystemMetricForDpi(SM_CYSMICON, windowDpi);
        int bigIconWidth = GetSystemMetricForDpi(SM_CXICON, windowDpi);
        int bigIconHeight = GetSystemMetricForDpi(SM_CYICON, windowDpi);

        m_hSmallIcon = LoadApplicationIcon(smallIconWidth, smallIconHeight, false);
        m_hBigIcon = LoadApplicationIcon(bigIconWidth, bigIconHeight, false);

        if (m_hSmallIcon) {
            SendMessageW(m_hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(m_hSmallIcon));
            SendMessageW(m_hWnd, WM_SETICON, ICON_SMALL2, reinterpret_cast<LPARAM>(m_hSmallIcon));
        }

        if (m_hBigIcon) {
            SendMessageW(m_hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(m_hBigIcon));
        }
    }

    bool DrawTrackbarPart(NMCUSTOMDRAW* customDraw) {
        if (customDraw->dwItemSpec == TBCD_CHANNEL) {
            DrawTrackbarChannel(customDraw->hdc);
            return true;
        }

        if (customDraw->dwItemSpec == TBCD_THUMB) {
            DrawTrackbarThumb(customDraw->hdc);
            return true;
        }

        if (customDraw->dwItemSpec == TBCD_TICS) {
            return true;
        }

        return false;
    }

    void DrawTrackbarChannel(HDC deviceContext) {
        RECT clientRectangle = { 0, 0, 0, 0 };
        GetClientRect(m_hTrackbar, &clientRectangle);

        RECT thumbRectangle = { 0, 0, 0, 0 };
        SendMessageW(m_hTrackbar, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumbRectangle));

        int trackLeft = 10;
        int trackRight = clientRectangle.right - 10;
        int trackCenterY = (clientRectangle.bottom - clientRectangle.top) / 2;
        RECT trackRectangle = { trackLeft, trackCenterY - 3, trackRight, trackCenterY + 3 };

        HBRUSH backgroundBrush = CreateSolidBrush(kTrackBackgroundColor);
        HBRUSH accentBrush = CreateSolidBrush(kAccentColor);
        HPEN backgroundPen = CreatePen(PS_SOLID, 1, kTrackBackgroundColor);
        HPEN accentPen = CreatePen(PS_SOLID, 1, kAccentColor);

        HGDIOBJ previousBrush = SelectObject(deviceContext, backgroundBrush);
        HGDIOBJ previousPen = SelectObject(deviceContext, backgroundPen);
        RoundRect(deviceContext, trackRectangle.left, trackRectangle.top, trackRectangle.right, trackRectangle.bottom, 6, 6);

        int thumbCenterX = (thumbRectangle.left + thumbRectangle.right) / 2;
        if (thumbCenterX > trackRectangle.left) {
            RECT fillRectangle = { trackRectangle.left, trackRectangle.top, thumbCenterX, trackRectangle.bottom };
            SelectObject(deviceContext, accentBrush);
            SelectObject(deviceContext, accentPen);
            RoundRect(deviceContext, fillRectangle.left, fillRectangle.top, fillRectangle.right, fillRectangle.bottom, 6, 6);
        }

        SelectObject(deviceContext, previousBrush);
        SelectObject(deviceContext, previousPen);
        DeleteObject(backgroundBrush);
        DeleteObject(accentBrush);
        DeleteObject(backgroundPen);
        DeleteObject(accentPen);
    }

    void DrawTrackbarThumb(HDC deviceContext) {
        RECT thumbRectangle = { 0, 0, 0, 0 };
        SendMessageW(m_hTrackbar, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumbRectangle));
        InflateRect(&thumbRectangle, 1, 1);

        HBRUSH thumbBrush = CreateSolidBrush(kWindowBackgroundColor);
        HPEN thumbPen = CreatePen(PS_SOLID, 2, kAccentColor);

        HGDIOBJ previousBrush = SelectObject(deviceContext, thumbBrush);
        HGDIOBJ previousPen = SelectObject(deviceContext, thumbPen);
        RoundRect(deviceContext, thumbRectangle.left, thumbRectangle.top, thumbRectangle.right, thumbRectangle.bottom, 8, 8);

        SelectObject(deviceContext, previousBrush);
        SelectObject(deviceContext, previousPen);
        DeleteObject(thumbBrush);
        DeleteObject(thumbPen);
    }

    LRESULT HandleControlColor(HDC deviceContext) {
        SetBkMode(deviceContext, TRANSPARENT);
        SetTextColor(deviceContext, kTextColor);
        return reinterpret_cast<LRESULT>(m_hBackgroundBrush);
    }

    bool CreateMainWindow() {
        INITCOMMONCONTROLSEX commonControls = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&commonControls);

        UINT systemDpi = GetDpiForSystem();
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = m_hInstance;
        wc.hIcon = LoadApplicationIcon(GetSystemMetricForDpi(SM_CXICON, systemDpi), GetSystemMetricForDpi(SM_CYICON, systemDpi), true);
        wc.hIconSm = LoadApplicationIcon(GetSystemMetricForDpi(SM_CXSMICON, systemDpi), GetSystemMetricForDpi(SM_CYSMICON, systemDpi), true);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = m_hBackgroundBrush;
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

        UpdateWindowIcons();

        ShowWindow(m_hWnd, SW_SHOWNORMAL);
        UpdateWindow(m_hWnd);
        return true;
    }

    void CreateChildControls(HWND hWnd) {
        m_hDescriptionLabel = CreateWindowExW(0, L"STATIC", L"Регулировка уровня громкости микрофона. Изменения применяются сразу.", WS_CHILD | WS_VISIBLE, 24, 20, 372, 36, hWnd, ControlIdToMenuHandle(kDescriptionLabelId), m_hInstance, NULL);

        m_hVolumeValueLabel = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 24, 70, 240, 22, hWnd, ControlIdToMenuHandle(kVolumeValueLabelId), m_hInstance, NULL);

        m_hTrackbar = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_NOTICKS | TBS_TOOLTIPS | TBS_TRANSPARENTBKGND, 24, 100, 372, 42, hWnd, ControlIdToMenuHandle(kVolumeTrackbarId), m_hInstance, NULL);
        if (m_hTrackbar) {
            SendMessageW(m_hTrackbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(m_hTrackbar, TBM_SETPAGESIZE, 0, 10);
            SendMessageW(m_hTrackbar, TBM_SETLINESIZE, 0, 1);
        }

        m_hVersionLabel = CreateWindowExW(0, L"STATIC", L"Версия 0.1.5", WS_CHILD | WS_VISIBLE | SS_RIGHT, 240, 154, 156, 20, hWnd, ControlIdToMenuHandle(kVersionLabelId), m_hInstance, NULL);

        HFONT dialogFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessageW(m_hDescriptionLabel, WM_SETFONT, reinterpret_cast<WPARAM>(dialogFont), TRUE);
        SendMessageW(m_hVolumeValueLabel, WM_SETFONT, reinterpret_cast<WPARAM>(dialogFont), TRUE);
        SendMessageW(m_hTrackbar, WM_SETFONT, reinterpret_cast<WPARAM>(dialogFont), TRUE);
        SendMessageW(m_hVersionLabel, WM_SETFONT, reinterpret_cast<WPARAM>(dialogFont), TRUE);

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
            case WM_NOTIFY:
                if (reinterpret_cast<LPNMHDR>(lParam)->hwndFrom == app->m_hTrackbar && reinterpret_cast<LPNMHDR>(lParam)->code == NM_CUSTOMDRAW) {
                    NMCUSTOMDRAW* customDraw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
                    if (customDraw->dwDrawStage == CDDS_PREPAINT) {
                        FillRect(customDraw->hdc, &customDraw->rc, app->m_hBackgroundBrush);
                        return CDRF_NOTIFYITEMDRAW;
                    }
                    if (customDraw->dwDrawStage == CDDS_ITEMPREPAINT && app->DrawTrackbarPart(customDraw)) {
                        return CDRF_SKIPDEFAULT;
                    }
                }
                break;
            case WM_CTLCOLORSTATIC:
                return app->HandleControlColor(reinterpret_cast<HDC>(wParam));
            case WM_DPICHANGED:
                app->UpdateWindowIcons();
                break;
            case WM_ERASEBKGND:
                {
                    RECT clientRectangle = { 0, 0, 0, 0 };
                    GetClientRect(hWnd, &clientRectangle);
                    FillRect(reinterpret_cast<HDC>(wParam), &clientRectangle, app->m_hBackgroundBrush);
                }
                return 1;
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
                app->DestroyWindowIcons();
                if (app->m_allowExit) {
                    PostQuitMessage(0);
                }
                return 0;
            }
        }
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    HINSTANCE m_hInstance = NULL;
    HBRUSH m_hBackgroundBrush = NULL;
    HWND m_hWnd = NULL;
    HWND m_hDescriptionLabel = NULL;
    HWND m_hVolumeValueLabel = NULL;
    HWND m_hTrackbar = NULL;
    HWND m_hVersionLabel = NULL;
    HICON m_hSmallIcon = NULL;
    HICON m_hBigIcon = NULL;
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
