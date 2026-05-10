#include "TrayManager.hpp"
#include "resource.h"

TrayManager::TrayManager(HINSTANCE hInstance) : m_hInstance(hInstance) {}

TrayManager::~TrayManager() {
    Shell_NotifyIconW(NIM_DELETE, &m_nid);
}

bool TrayManager::CreateTrayIcon(HWND hWnd) {
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = hWnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAY_ICON;
    m_nid.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wcscpy_s(m_nid.szTip, L"MicLock - Microphone Volume Enforcer");

    return Shell_NotifyIconW(NIM_ADD, &m_nid) == TRUE;
}

void TrayManager::SetEnabledState(bool enabled) {
    m_enabled = enabled;
}

void TrayManager::ShowContextMenu(HWND hWnd) {
    HMENU hMenu = CreatePopupMenu();
    if (hMenu) {
        AppendMenuW(hMenu, MF_STRING | (m_enabled ? MF_CHECKED : 0), 1, L"Enabled");
        AppendMenuW(hMenu, MF_STRING, 2, L"Open Config");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, 3, L"Exit");

        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(hWnd);
        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);

        if (cmd == 1 && m_onToggle) m_onToggle();
        else if (cmd == 2 && m_onOpenConfig) m_onOpenConfig();
        else if (cmd == 3 && m_onExit) m_onExit();

        DestroyMenu(hMenu);
    }
}
