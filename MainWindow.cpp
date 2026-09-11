#include "pch.h"
#include "MainWindow.hpp"
#include "resource.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <string>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {
    constexpr const wchar_t* kClassName = L"MicGainControl_MainWindowClass";
    constexpr INT_PTR ID_SLIDER = 1001;
    constexpr INT_PTR ID_TOGGLE = 1002;
    constexpr INT_PTR ID_GITHUB_LINK = 1003;

    bool IsSystemDarkMode() {
        DWORD data = 1;
        DWORD dataSize = sizeof(data);
        LONG res = RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme",
            RRF_RT_REG_DWORD,
            nullptr,
            &data,
            &dataSize
        );
        if (res == ERROR_SUCCESS) {
            return data == 0;
        }
        return false;
    }
}

MainWindow::MainWindow(HINSTANCE hInstance)
    : m_hInstance(hInstance), m_trayManager(hInstance) {
}

MainWindow::~MainWindow() {
    if (m_hFontTitle) DeleteObject(m_hFontTitle);
    if (m_hFontNormal) DeleteObject(m_hFontNormal);
    if (m_hFontBold) DeleteObject(m_hFontBold);
    if (m_hFontCaption) DeleteObject(m_hFontCaption);
    if (m_hBgBrush) DeleteObject(m_hBgBrush);
}

bool MainWindow::Initialize(int nCmdShow) {
    INITCOMMONCONTROLSEX icex = { 0 };
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_LINK_CLASS;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWindow::WindowProc;
    wc.hInstance = m_hInstance;
    wc.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // Handled in WM_ERASEBKGND / WM_CTLCOLORSTATIC
    wc.lpszClassName = kClassName;

    RegisterClassExW(&wc);

    int width = 480;
    int height = 280;

    RECT rc = { 0, 0, width, height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX), FALSE);
    int windowWidth = rc.right - rc.left;
    int windowHeight = rc.bottom - rc.top;

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenWidth - windowWidth) / 2;
    int posY = (screenHeight - windowHeight) / 2;

    m_hWnd = CreateWindowExW(
        0,
        kClassName,
        L"MicGainControl",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY, windowWidth, windowHeight,
        nullptr, nullptr, m_hInstance, this
    );

    if (!m_hWnd) {
        return false;
    }

    UpdateTheme();
    CreateControls();

    if (m_configManager.Load() && m_audioManager.Initialize()) {
        ApplyConfigToUI(m_configManager.GetConfig());

        m_configManager.SetCallback([this](const Config& cfg) {
            PostMessageW(m_hWnd, WM_CONFIG_CHANGED, 0, 0);
        });
        m_configManager.StartWatching();
    }

    m_trayManager.SetOnToggle([this]() {
        bool newState = !m_configManager.GetConfig().enabled;
        m_configManager.SetEnabled(newState);
        m_audioManager.SetEnabled(newState);
        ApplyConfigToUI(m_configManager.GetConfig());
    });

    m_trayManager.SetOnOpenConfig([this]() {
        ShowWindow(true);
    });

    m_trayManager.SetOnExit([this]() {
        Exit();
    });

    m_trayManager.CreateTrayIcon(m_hWnd);

    if (nCmdShow != SW_HIDE) {
        ::ShowWindow(m_hWnd, nCmdShow);
        UpdateWindow(m_hWnd);
    }

    return true;
}

void MainWindow::ShowWindow(bool show) {
    if (show) {
        ::ShowWindow(m_hWnd, SW_SHOW);
        ::ShowWindow(m_hWnd, SW_RESTORE);
        SetForegroundWindow(m_hWnd);
    } else {
        ::ShowWindow(m_hWnd, SW_HIDE);
    }
}

void MainWindow::Exit() {
    m_allowExit = true;
    m_trayManager.RemoveTrayIcon();
    DestroyWindow(m_hWnd);
}

void MainWindow::UpdateTheme() {
    m_isDarkMode = IsSystemDarkMode();
    BOOL useDark = m_isDarkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));

    if (m_hBgBrush) {
        DeleteObject(m_hBgBrush);
    }

    if (m_isDarkMode) {
        m_bgColor = RGB(32, 32, 32);
        m_textColor = RGB(255, 255, 255);
        m_subTextColor = RGB(160, 160, 160);
    } else {
        m_bgColor = RGB(243, 243, 243);
        m_textColor = RGB(0, 0, 0);
        m_subTextColor = RGB(96, 96, 96);
    }

    m_hBgBrush = CreateSolidBrush(m_bgColor);
    InvalidateRect(m_hWnd, nullptr, TRUE);
}

void MainWindow::CreateControls() {
    NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);

    LOGFONTW lf = ncm.lfMessageFont;
    m_hFontNormal = CreateFontIndirectW(&lf);

    lf.lfWeight = FW_BOLD;
    m_hFontBold = CreateFontIndirectW(&lf);

    lf.lfHeight = -18;
    m_hFontTitle = CreateFontIndirectW(&lf);

    lf.lfHeight = -12;
    lf.lfWeight = FW_NORMAL;
    m_hFontCaption = CreateFontIndirectW(&lf);

    // Header Title & Subtitle
    m_hTitleText = CreateWindowExW(
        0, L"STATIC", L"MicGainControl",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        24, 20, 300, 24,
        m_hWnd, nullptr, m_hInstance, nullptr
    );
    SendMessageW(m_hTitleText, WM_SETFONT, (WPARAM)m_hFontTitle, TRUE);

    m_hSubTitleText = CreateWindowExW(
        0, L"STATIC", L"Изменения применяются сразу",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        24, 46, 300, 18,
        m_hWnd, nullptr, m_hInstance, nullptr
    );
    SendMessageW(m_hSubTitleText, WM_SETFONT, (WPARAM)m_hFontCaption, TRUE);

    // Volume Section
    m_hVolumeLabel = CreateWindowExW(
        0, L"STATIC", L"Громкость микрофона",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        24, 80, 200, 20,
        m_hWnd, nullptr, m_hInstance, nullptr
    );
    SendMessageW(m_hVolumeLabel, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);

    m_hVolumeValueText = CreateWindowExW(
        0, L"STATIC", L"100%",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        380, 80, 76, 20,
        m_hWnd, nullptr, m_hInstance, nullptr
    );
    SendMessageW(m_hVolumeValueText, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);

    m_hVolumeSlider = CreateWindowExW(
        0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS | TBS_NOTICKS,
        20, 105, 436, 30,
        m_hWnd, (HMENU)ID_SLIDER, m_hInstance, nullptr
    );
    SendMessageW(m_hVolumeSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessageW(m_hVolumeSlider, TBM_SETPOS, TRUE, 100);

    // Enabled Toggle
    m_hEnabledLabel = CreateWindowExW(
        0, L"STATIC", L"Микрофон",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        24, 150, 200, 20,
        m_hWnd, nullptr, m_hInstance, nullptr
    );
    SendMessageW(m_hEnabledLabel, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);

    m_hEnabledToggle = CreateWindowExW(
        0, L"BUTTON", L"Вкл",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        380, 145, 76, 30,
        m_hWnd, (HMENU)ID_TOGGLE, m_hInstance, nullptr
    );
    SendMessageW(m_hEnabledToggle, WM_SETFONT, (WPARAM)m_hFontNormal, TRUE);
    SendMessageW(m_hEnabledToggle, BM_SETCHECK, BST_CHECKED, 0);

    // Footer
    m_hGithubLink = CreateWindowExW(
        0, WC_LINK, L"<a href=\"https://github.com/megoRU/MicGainControl/releases\">GitHub</a>",
        WS_CHILD | WS_VISIBLE,
        24, 210, 100, 20,
        m_hWnd, (HMENU)ID_GITHUB_LINK, m_hInstance, nullptr
    );
    SendMessageW(m_hGithubLink, WM_SETFONT, (WPARAM)m_hFontCaption, TRUE);

    m_hVersionText = CreateWindowExW(
        0, L"STATIC", L"Версия 0.1.6",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        330, 210, 126, 20,
        m_hWnd, nullptr, m_hInstance, nullptr
    );
    SendMessageW(m_hVersionText, WM_SETFONT, (WPARAM)m_hFontCaption, TRUE);
}

void MainWindow::ApplyConfigToUI(const Config& cfg) {
    m_updatingUI = true;

    m_audioManager.SetTargetVolume(cfg.microphoneVolume);
    m_audioManager.SetEnabled(cfg.enabled);
    m_trayManager.SetEnabledState(cfg.enabled);

    int volPercent = static_cast<int>((cfg.microphoneVolume * 100.0f) + 0.5f);
    if (volPercent < 0) volPercent = 0;
    if (volPercent > 100) volPercent = 100;

    SendMessageW(m_hVolumeSlider, TBM_SETPOS, TRUE, volPercent);
    SetWindowTextW(m_hVolumeValueText, (std::to_wstring(volPercent) + L"%").c_str());

    EnableWindow(m_hVolumeSlider, cfg.enabled ? TRUE : FALSE);

    SendMessageW(m_hEnabledToggle, BM_SETCHECK, cfg.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(m_hEnabledToggle, cfg.enabled ? L"Вкл" : L"Выкл");

    m_updatingUI = false;
}

void MainWindow::OnSliderScroll() {
    if (m_updatingUI) return;

    int volPercent = (int)SendMessageW(m_hVolumeSlider, TBM_GETPOS, 0, 0);
    float volume = static_cast<float>(volPercent) / 100.0f;

    SetWindowTextW(m_hVolumeValueText, (std::to_wstring(volPercent) + L"%").c_str());

    m_configManager.SetMicrophoneVolume(volume);
    m_audioManager.ApplyVolumeImmediately(volume);
}

void MainWindow::OnToggleClick() {
    if (m_updatingUI) return;

    bool enabled = (SendMessageW(m_hEnabledToggle, BM_GETCHECK, 0, 0) == BST_CHECKED);
    SetWindowTextW(m_hEnabledToggle, enabled ? L"Вкл" : L"Выкл");
    EnableWindow(m_hVolumeSlider, enabled ? TRUE : FALSE);

    m_configManager.SetEnabled(enabled);
    ApplyConfigToUI(m_configManager.GetConfig());
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hWnd = hWnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(uMsg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_TRAY_ICON:
        if (LOWORD(lParam) == WM_RBUTTONUP) {
            m_trayManager.ShowContextMenu(m_hWnd);
        } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            ShowWindow(true);
        }
        return 0;

    case WM_CONFIG_CHANGED:
        ApplyConfigToUI(m_configManager.GetConfig());
        return 0;

    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == m_hVolumeSlider) {
            OnSliderScroll();
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TOGGLE && HIWORD(wParam) == BN_CLICKED) {
            OnToggleClick();
        }
        return 0;

    case WM_NOTIFY: {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->idFrom == ID_GITHUB_LINK && (pnmh->code == NM_CLICK || pnmh->code == NM_RETURN)) {
            PNMLINK pNmLi = (PNMLINK)lParam;
            ShellExecuteW(NULL, L"open", pNmLi->item.szUrl, NULL, NULL, SW_SHOWNORMAL);
            return TRUE;
        }
        break;
    }

    case WM_SETTINGCHANGE:
        if (lParam && wcscmp((LPCWSTR)lParam, L"ImmersiveColorSet") == 0) {
            UpdateTheme();
        }
        break;

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        FillRect(hdc, &rc, m_hBgBrush);
        return 1;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        SetBkMode(hdcStatic, TRANSPARENT);

        if (hCtrl == m_hSubTitleText || hCtrl == m_hVersionText) {
            SetTextColor(hdcStatic, m_subTextColor);
        } else {
            SetTextColor(hdcStatic, m_textColor);
        }
        return (INT_PTR)m_hBgBrush;
    }

    case WM_CTLCOLORBTN: {
        HDC hdcBtn = (HDC)wParam;
        SetBkMode(hdcBtn, TRANSPARENT);
        SetTextColor(hdcBtn, m_textColor);
        return (INT_PTR)m_hBgBrush;
    }

    case WM_CLOSE:
        if (!m_allowExit) {
            ShowWindow(false);
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
}
