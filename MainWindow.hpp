#pragma once

#include <windows.h>
#include <commctrl.h>
#include "AudioManager.hpp"
#include "ConfigManager.hpp"
#include "TrayManager.hpp"

#define WM_CONFIG_CHANGED (WM_USER + 2)

class MainWindow {
public:
    MainWindow(HINSTANCE hInstance);
    ~MainWindow();

    bool Initialize(int nCmdShow);
    HWND GetHwnd() const { return m_hWnd; }
    void ShowWindow(bool show);
    void Exit();

private:
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void UpdateTheme();
    void ApplyConfigToUI(const Config& cfg);
    void OnSliderScroll();
    void OnToggleClick();

    HINSTANCE m_hInstance;
    HWND m_hWnd = nullptr;

    HWND m_hIconHeader = nullptr;
    HWND m_hTitleText = nullptr;
    HWND m_hSubTitleText = nullptr;

    HWND m_hVolumeLabel = nullptr;
    HWND m_hVolumeValueText = nullptr;
    HWND m_hVolumeSlider = nullptr;

    HWND m_hEnabledLabel = nullptr;
    HWND m_hEnabledToggle = nullptr;

    HWND m_hGithubLink = nullptr;
    HWND m_hVersionText = nullptr;

    HFONT m_hFontTitle = nullptr;
    HFONT m_hFontNormal = nullptr;
    HFONT m_hFontBold = nullptr;
    HFONT m_hFontCaption = nullptr;

    HBRUSH m_hBgBrush = nullptr;
    COLORREF m_bgColor = RGB(255, 255, 255);
    COLORREF m_textColor = RGB(0, 0, 0);
    COLORREF m_subTextColor = RGB(100, 100, 100);
    bool m_isDarkMode = false;

    bool m_allowExit = false;
    bool m_updatingUI = false;

    AudioManager m_audioManager;
    ConfigManager m_configManager;
    TrayManager m_trayManager;
};
