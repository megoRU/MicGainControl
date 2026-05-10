#pragma once

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <functional>

#define WM_TRAY_ICON (WM_USER + 1)

class TrayManager {
public:
    TrayManager(HINSTANCE hInstance);
    ~TrayManager();

    bool CreateTrayIcon(HWND hWnd);
    void ShowContextMenu(HWND hWnd);

    void SetEnabledState(bool enabled);

    using CommandCallback = std::function<void()>;
    void SetOnExit(CommandCallback callback) { m_onExit = callback; }
    void SetOnToggle(CommandCallback callback) { m_onToggle = callback; }
    void SetOnOpenConfig(CommandCallback callback) { m_onOpenConfig = callback; }

private:
    HINSTANCE m_hInstance;
    NOTIFYICONDATAW m_nid = {0};
    bool m_enabled = true;

    CommandCallback m_onExit;
    CommandCallback m_onToggle;
    CommandCallback m_onOpenConfig;
};
