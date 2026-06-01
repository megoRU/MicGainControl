#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <windowsx.h>
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

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {
constexpr int kBaseWindowClientWidth = 520;
constexpr int kBaseWindowClientHeight = 230;
constexpr int kMinimumWindowClientWidth = 460;
constexpr int kMinimumWindowClientHeight = 220;
constexpr int kContentMargin = 20;
constexpr int kTrackbarId = 1002;
constexpr int kTitleLabelId = 1003;
constexpr int kDescriptionLabelId = 1004;
constexpr int kVolumeCaptionLabelId = 1005;
constexpr int kVolumeValueLabelId = 1006;
constexpr int kVersionLabelId = 1007;
constexpr int kGithubLinkId = 1008;
constexpr int kEnabledToggleId = 1009;
constexpr int kTrackThumbDiameter = 26;
constexpr int kTrackChannelHeight = 14;
constexpr UINT kApplyExternalConfigMessage = WM_APP + 1;
constexpr wchar_t kGithubReleasesUrl[] = L"https://github.com/megoRU/MicGainControl/releases";

struct ThemeColors {
    COLORREF backgroundColor;
    COLORREF textColor;
    COLORREF secondaryTextColor;
    COLORREF trackColor;
    COLORREF accentColor;
    COLORREF thumbFillColor;
    COLORREF thumbBorderColor;
};

struct TrackbarGeometry {
    Gdiplus::RectF trackRectangle;
    float thumbCenterX;
    float thumbCenterY;
    float thumbDiameter;
};

int ClampPercent(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

int VolumeToPercent(float volume) {
    return ClampPercent(static_cast<int>((volume * 100.0f) + 0.5f));
}

std::wstring MakeVolumePercentText(int volumePercent) {
    return std::to_wstring(ClampPercent(volumePercent)) + L"%";
}

HMENU ControlIdToMenuHandle(int controlId) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId));
}

Gdiplus::Color ToGdiplusColor(COLORREF color) {
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

void AddCapsulePath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rectangle) {
    if (rectangle.Width <= 0.0f || rectangle.Height <= 0.0f) {
        return;
    }

    if (rectangle.Width <= rectangle.Height) {
        path.AddEllipse(rectangle);
        path.CloseFigure();
        return;
    }

    float diameter = rectangle.Height;
    float radius = diameter / 2.0f;
    float left = rectangle.X;
    float top = rectangle.Y;
    float right = rectangle.X + rectangle.Width;
    float bottom = rectangle.Y + rectangle.Height;

    path.AddArc(left, top, diameter, diameter, 90.0f, 180.0f);
    path.AddLine(left + radius, top, right - radius, top);
    path.AddArc(right - diameter, top, diameter, diameter, 270.0f, 180.0f);
    path.AddLine(right - radius, bottom, left + radius, bottom);
    path.CloseFigure();
}

bool IsWindowsDarkThemeEnabled() {
    DWORD appsUseLightTheme = 1;
    DWORD valueSize = sizeof(appsUseLightTheme);
    LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &appsUseLightTheme,
        &valueSize
    );

    if (status != ERROR_SUCCESS) {
        return false;
    }

    return appsUseLightTheme == 0;
}
}

class Application {
public:
    Application(HINSTANCE hInstance) : m_hInstance(hInstance), m_trayManager(hInstance) {}

    ~Application() {
        DestroyWindowIcons();
        DestroyFonts();
        DestroyBrushes();
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
    int Scale(int value) const {
        return MulDiv(value, static_cast<int>(m_dpi), 96);
    }

    int PointsToPixels(int points) const {
        return -MulDiv(points, static_cast<int>(m_dpi), 72);
    }

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

    void DestroyFonts() {
        if (m_hTitleFont) {
            DeleteObject(m_hTitleFont);
            m_hTitleFont = NULL;
        }
        if (m_hTextFont) {
            DeleteObject(m_hTextFont);
            m_hTextFont = NULL;
        }
        if (m_hSecondaryFont) {
            DeleteObject(m_hSecondaryFont);
            m_hSecondaryFont = NULL;
        }
        if (m_hLinkFont) {
            DeleteObject(m_hLinkFont);
            m_hLinkFont = NULL;
        }
    }

    HFONT CreateSegoeFont(int pointSize, LONG weight, bool underline) const {
        LOGFONTW logFont = {};
        logFont.lfHeight = PointsToPixels(pointSize);
        logFont.lfWeight = weight;
        logFont.lfUnderline = underline ? TRUE : FALSE;
        logFont.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(logFont.lfFaceName, L"Segoe UI");
        return CreateFontIndirectW(&logFont);
    }

    void RecreateFonts() {
        DestroyFonts();
        m_hTitleFont = CreateSegoeFont(11, FW_SEMIBOLD, false);
        m_hTextFont = CreateSegoeFont(9, FW_NORMAL, false);
        m_hSecondaryFont = CreateSegoeFont(10, FW_NORMAL, false);
        m_hLinkFont = CreateSegoeFont(10, FW_NORMAL, true);
        ApplyFontsToControls();
    }

    void ApplyFontsToControls() {
        if (m_hTitleLabel && m_hTitleFont) {
            SendMessageW(m_hTitleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_hTitleFont), TRUE);
        }
        if (m_hDescriptionLabel && m_hSecondaryFont) {
            SendMessageW(m_hDescriptionLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_hSecondaryFont), TRUE);
        }
        if (m_hVolumeCaptionLabel && m_hTextFont) {
            SendMessageW(m_hVolumeCaptionLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_hTextFont), TRUE);
        }
        if (m_hVolumeValueLabel && m_hTitleFont) {
            SendMessageW(m_hVolumeValueLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_hTitleFont), TRUE);
        }
        if (m_hVersionLabel && m_hSecondaryFont) {
            SendMessageW(m_hVersionLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_hSecondaryFont), TRUE);
        }
        if (m_hGithubLink && m_hLinkFont) {
            SendMessageW(m_hGithubLink, WM_SETFONT, reinterpret_cast<WPARAM>(m_hLinkFont), TRUE);
        }
    }

    void DestroyBrushes() {
        if (m_hBackgroundBrush) {
            DeleteObject(m_hBackgroundBrush);
            m_hBackgroundBrush = NULL;
        }
    }

    ThemeColors GetThemeColors(bool darkModeEnabled) const {
        if (darkModeEnabled) {
            return ThemeColors{
                RGB(32, 32, 32),
                RGB(243, 243, 243),
                RGB(200, 200, 200),
                RGB(72, 72, 72),
                RGB(96, 205, 255),
                RGB(96, 205, 255),
                RGB(245, 245, 245)
            };
        }

        return ThemeColors{
            RGB(249, 249, 249),
            RGB(32, 32, 32),
            RGB(96, 96, 96),
            RGB(214, 218, 224),
            RGB(0, 120, 215),
            RGB(0, 120, 215),
            RGB(255, 255, 255)
        };
    }

    void RefreshTheme() {
        m_darkModeEnabled = IsWindowsDarkThemeEnabled();
        m_themeColors = GetThemeColors(m_darkModeEnabled);

        DestroyBrushes();
        m_hBackgroundBrush = CreateSolidBrush(m_themeColors.backgroundColor);

        if (m_hWnd) {
            BOOL useDarkMode = m_darkModeEnabled ? TRUE : FALSE;
            DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
        }

        ApplyThemeToControls();
    }

    void ApplyThemeToControls() {
        LPCWSTR controlTheme = m_darkModeEnabled ? L"DarkMode_Explorer" : L"Explorer";
        if (m_hTrackbar) {
            SetWindowTheme(m_hTrackbar, controlTheme, nullptr);
        }

        if (m_hWnd) {
            InvalidateRect(m_hWnd, nullptr, FALSE);
        }
        if (m_hTrackbar) {
            InvalidateRect(m_hTrackbar, nullptr, FALSE);
        }
        if (m_hEnabledToggle) {
            InvalidateRect(m_hEnabledToggle, nullptr, FALSE);
        }
    }

    void LayoutControls() {
        if (!m_hWnd) {
            return;
        }

        RECT clientRectangle = { 0, 0, 0, 0 };
        GetClientRect(m_hWnd, &clientRectangle);

        int margin = Scale(kContentMargin);
        int contentWidth = clientRectangle.right - clientRectangle.left - (margin * 2);
        int titleHeight = Scale(24);
        int toggleWidth = Scale(132);
        int descriptionHeight = Scale(34);
        int captionHeight = Scale(22);
        int valueWidth = Scale(72);
        int trackbarHeight = Scale(42);
        int footerHeight = Scale(22);
        int linkWidth = Scale(160);
        int y = margin;

        MoveWindow(m_hTitleLabel, margin, y, contentWidth - toggleWidth - Scale(12), titleHeight, TRUE);
        MoveWindow(m_hEnabledToggle, clientRectangle.right - margin - toggleWidth, y - Scale(2), toggleWidth, Scale(30), TRUE);
        y += titleHeight + Scale(4);

        MoveWindow(m_hDescriptionLabel, margin, y, contentWidth, descriptionHeight, TRUE);
        y += descriptionHeight + Scale(12);

        MoveWindow(m_hVolumeCaptionLabel, margin, y, contentWidth - valueWidth - Scale(12), captionHeight, TRUE);
        MoveWindow(m_hVolumeValueLabel, clientRectangle.right - margin - valueWidth, y, valueWidth, captionHeight, TRUE);
        y += captionHeight + Scale(4);

        MoveWindow(m_hTrackbar, margin, y, contentWidth, trackbarHeight, TRUE);

        MoveWindow(m_hGithubLink, margin, clientRectangle.bottom - margin - footerHeight, linkWidth, footerHeight, TRUE);
        MoveWindow(m_hVersionLabel, margin + linkWidth + Scale(8), clientRectangle.bottom - margin - footerHeight, contentWidth - linkWidth - Scale(8), footerHeight, TRUE);
    }

    void SubclassTrackbar() {
        if (!m_hTrackbar || m_originalTrackbarProc) {
            return;
        }

        SetWindowLongPtrW(m_hTrackbar, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        m_originalTrackbarProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(m_hTrackbar, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(TrackbarProc)));
    }

    void RestoreTrackbarSubclass() {
        if (!m_hTrackbar || !m_originalTrackbarProc) {
            return;
        }

        SetWindowLongPtrW(m_hTrackbar, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_originalTrackbarProc));
        SetWindowLongPtrW(m_hTrackbar, GWLP_USERDATA, 0);
        m_originalTrackbarProc = nullptr;
    }

    void PaintTrackbar(HDC deviceContext) {
        RECT clientRectangle = { 0, 0, 0, 0 };
        GetClientRect(m_hTrackbar, &clientRectangle);

        int width = clientRectangle.right - clientRectangle.left;
        int height = clientRectangle.bottom - clientRectangle.top;
        if (width <= 0 || height <= 0) {
            return;
        }

        HDC memoryDeviceContext = CreateCompatibleDC(deviceContext);
        HBITMAP memoryBitmap = CreateCompatibleBitmap(deviceContext, width, height);
        HGDIOBJ previousBitmap = SelectObject(memoryDeviceContext, memoryBitmap);

        FillRect(memoryDeviceContext, &clientRectangle, m_hBackgroundBrush);
        DrawTrackbarChannel(memoryDeviceContext);
        DrawTrackbarThumb(memoryDeviceContext);
        BitBlt(deviceContext, 0, 0, width, height, memoryDeviceContext, 0, 0, SRCCOPY);

        SelectObject(memoryDeviceContext, previousBitmap);
        DeleteObject(memoryBitmap);
        DeleteDC(memoryDeviceContext);
    }

    void SetTrackbarPositionFromPoint(LPARAM lParam, bool applyImmediately) {
        if (!m_hTrackbar) {
            return;
        }

        TrackbarGeometry geometry = GetTrackbarGeometry();
        float x = static_cast<float>(GET_X_LPARAM(lParam));
        float trackStart = geometry.trackRectangle.X;
        float trackEnd = geometry.trackRectangle.X + geometry.trackRectangle.Width;
        if (x < trackStart) {
            x = trackStart;
        }
        if (x > trackEnd) {
            x = trackEnd;
        }

        float ratio = 0.0f;
        if (geometry.trackRectangle.Width > 0.0f) {
            ratio = (x - trackStart) / geometry.trackRectangle.Width;
        }

        int volumePercent = ClampPercent(static_cast<int>((ratio * 100.0f) + 0.5f));
        SendMessageW(m_hTrackbar, TBM_SETPOS, FALSE, volumePercent);
        if (applyImmediately) {
            HandleTrackbarChanged();
        } else {
            UpdateVolumeLabel(volumePercent);
            InvalidateRect(m_hTrackbar, nullptr, FALSE);
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

    TrackbarGeometry GetTrackbarGeometry() const {
        RECT clientRectangle = { 0, 0, 0, 0 };
        GetClientRect(m_hTrackbar, &clientRectangle);

        float clientWidth = static_cast<float>(clientRectangle.right - clientRectangle.left);
        float clientHeight = static_cast<float>(clientRectangle.bottom - clientRectangle.top);
        float trackHeight = static_cast<float>(Scale(kTrackChannelHeight));
        if (trackHeight < 2.0f) {
            trackHeight = 2.0f;
        }

        float thumbDiameter = static_cast<float>(Scale(kTrackThumbDiameter));
        float minimumThumbDiameter = trackHeight * 1.5f;
        float maximumThumbDiameter = trackHeight * 2.0f;
        if (thumbDiameter < minimumThumbDiameter) {
            thumbDiameter = minimumThumbDiameter;
        }
        if (thumbDiameter > maximumThumbDiameter) {
            thumbDiameter = maximumThumbDiameter;
        }

        float trackLeft = thumbDiameter / 2.0f;
        float trackRight = clientWidth - (thumbDiameter / 2.0f);
        if (trackRight < trackLeft) {
            trackRight = trackLeft;
        }

        float trackCenterY = clientHeight / 2.0f;
        float trackTop = trackCenterY - (trackHeight / 2.0f);
        int volumePercent = ClampPercent(static_cast<int>(SendMessageW(m_hTrackbar, TBM_GETPOS, 0, 0)));
        float volumeRatio = static_cast<float>(volumePercent) / 100.0f;
        float trackWidth = trackRight - trackLeft;
        float thumbCenterX = trackLeft + (trackWidth * volumeRatio);

        return TrackbarGeometry{
            Gdiplus::RectF(trackLeft, trackTop, trackWidth, trackHeight),
            thumbCenterX,
            trackCenterY,
            thumbDiameter
        };
    }

    void ConfigureHighQualityGraphics(Gdiplus::Graphics& graphics) const {
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    }

    void DrawTrackbarChannel(HDC deviceContext) {
        Gdiplus::Graphics graphics(deviceContext);
        ConfigureHighQualityGraphics(graphics);

        TrackbarGeometry geometry = GetTrackbarGeometry();
        Gdiplus::GraphicsPath inactivePath;
        AddCapsulePath(inactivePath, geometry.trackRectangle);

        Gdiplus::SolidBrush inactiveBrush(ToGdiplusColor(m_themeColors.trackColor));
        graphics.FillPath(&inactiveBrush, &inactivePath);

        float activeWidth = geometry.thumbCenterX - geometry.trackRectangle.X;
        if (activeWidth > 0.0f) {
            Gdiplus::RectF activeRectangle(geometry.trackRectangle.X, geometry.trackRectangle.Y, activeWidth, geometry.trackRectangle.Height);
            Gdiplus::GraphicsPath activePath;
            AddCapsulePath(activePath, activeRectangle);
            Gdiplus::SolidBrush activeBrush(ToGdiplusColor(m_themeColors.accentColor));
            graphics.FillPath(&activeBrush, &activePath);
        }
    }

    void DrawTrackbarThumb(HDC deviceContext) {
        Gdiplus::Graphics graphics(deviceContext);
        ConfigureHighQualityGraphics(graphics);

        TrackbarGeometry geometry = GetTrackbarGeometry();
        float borderWidth = static_cast<float>(Scale(3));
        if (borderWidth < 2.0f) {
            borderWidth = 2.0f;
        }

        float thumbLeft = geometry.thumbCenterX - (geometry.thumbDiameter / 2.0f);
        float thumbTop = geometry.thumbCenterY - (geometry.thumbDiameter / 2.0f);
        Gdiplus::RectF outerThumbRectangle(thumbLeft, thumbTop, geometry.thumbDiameter, geometry.thumbDiameter);
        Gdiplus::RectF innerThumbRectangle(
            thumbLeft + borderWidth,
            thumbTop + borderWidth,
            geometry.thumbDiameter - (borderWidth * 2.0f),
            geometry.thumbDiameter - (borderWidth * 2.0f)
        );

        Gdiplus::SolidBrush borderBrush(ToGdiplusColor(m_themeColors.thumbBorderColor));
        Gdiplus::SolidBrush thumbBrush(ToGdiplusColor(m_themeColors.thumbFillColor));
        Gdiplus::Pen borderPen(ToGdiplusColor(m_themeColors.thumbBorderColor), 1.0f);

        graphics.FillEllipse(&borderBrush, outerThumbRectangle);
        graphics.FillEllipse(&thumbBrush, innerThumbRectangle);
        graphics.DrawEllipse(&borderPen, outerThumbRectangle);
    }

    void DrawEnabledToggle(const DRAWITEMSTRUCT* drawItem) {
        bool enabled = SendMessageW(drawItem->hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;
        RECT itemRectangle = drawItem->rcItem;
        FillRect(drawItem->hDC, &itemRectangle, m_hBackgroundBrush);

        HFONT previousFont = nullptr;
        if (m_hTextFont) {
            previousFont = reinterpret_cast<HFONT>(SelectObject(drawItem->hDC, m_hTextFont));
        }

        SetBkMode(drawItem->hDC, TRANSPARENT);
        SetTextColor(drawItem->hDC, m_themeColors.secondaryTextColor);

        RECT textRectangle = itemRectangle;
        textRectangle.right -= Scale(54);
        DrawTextW(drawItem->hDC, enabled ? L"Вкл" : L"Выкл", -1, &textRectangle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (previousFont) {
            SelectObject(drawItem->hDC, previousFont);
        }

        Gdiplus::Graphics graphics(drawItem->hDC);
        ConfigureHighQualityGraphics(graphics);

        float switchHeight = static_cast<float>(Scale(22));
        float switchWidth = static_cast<float>(Scale(44));
        float switchX = static_cast<float>(itemRectangle.right) - switchWidth;
        float switchY = static_cast<float>(itemRectangle.top + ((itemRectangle.bottom - itemRectangle.top) / 2)) - (switchHeight / 2.0f);
        Gdiplus::RectF switchRectangle(switchX, switchY, switchWidth, switchHeight);

        Gdiplus::GraphicsPath switchPath;
        AddCapsulePath(switchPath, switchRectangle);
        Gdiplus::SolidBrush switchBrush(ToGdiplusColor(enabled ? m_themeColors.accentColor : m_themeColors.trackColor));
        graphics.FillPath(&switchBrush, &switchPath);

        float knobInset = static_cast<float>(Scale(3));
        float knobDiameter = switchHeight - (knobInset * 2.0f);
        float knobX = enabled ? (switchX + switchWidth - knobInset - knobDiameter) : (switchX + knobInset);
        float knobY = switchY + knobInset;
        Gdiplus::RectF knobRectangle(knobX, knobY, knobDiameter, knobDiameter);
        Gdiplus::SolidBrush knobBrush(ToGdiplusColor(m_themeColors.thumbBorderColor));
        graphics.FillEllipse(&knobBrush, knobRectangle);
    }

    LRESULT HandleControlColor(HWND controlWindow, HDC deviceContext) {
        SetBkMode(deviceContext, TRANSPARENT);

        if (controlWindow == m_hGithubLink) {
            SetTextColor(deviceContext, m_themeColors.accentColor);
        } else if (controlWindow == m_hDescriptionLabel || controlWindow == m_hVersionLabel) {
            SetTextColor(deviceContext, m_themeColors.secondaryTextColor);
        } else {
            SetTextColor(deviceContext, m_themeColors.textColor);
        }

        return reinterpret_cast<LRESULT>(m_hBackgroundBrush);
    }

    bool CreateMainWindow() {
        INITCOMMONCONTROLSEX commonControls = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&commonControls);

        m_dpi = GetDpiForSystem();
        RefreshTheme();
        RecreateFonts();

        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = m_hInstance;
        wc.hIcon = LoadApplicationIcon(GetSystemMetricForDpi(SM_CXICON, m_dpi), GetSystemMetricForDpi(SM_CYICON, m_dpi), true);
        wc.hIconSm = LoadApplicationIcon(GetSystemMetricForDpi(SM_CXSMICON, m_dpi), GetSystemMetricForDpi(SM_CYSMICON, m_dpi), true);
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
        RECT windowRectangle = { 0, 0, Scale(kBaseWindowClientWidth), Scale(kBaseWindowClientHeight) };
        AdjustWindowRectExForDpi(&windowRectangle, windowStyle, FALSE, windowExStyle, m_dpi);

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
        RefreshTheme();

        ShowWindow(m_hWnd, SW_SHOWNORMAL);
        UpdateWindow(m_hWnd);
        return true;
    }

    void CreateChildControls(HWND hWnd) {
        m_hTitleLabel = CreateWindowExW(0, L"STATIC", L"Громкость микрофона", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, ControlIdToMenuHandle(kTitleLabelId), m_hInstance, NULL);
        m_hDescriptionLabel = CreateWindowExW(0, L"STATIC", L"Изменения применяются сразу и сохраняются в настройках приложения.", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, ControlIdToMenuHandle(kDescriptionLabelId), m_hInstance, NULL);
        m_hVolumeCaptionLabel = CreateWindowExW(0, L"STATIC", L"Уровень", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, ControlIdToMenuHandle(kVolumeCaptionLabelId), m_hInstance, NULL);
        m_hVolumeValueLabel = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0, hWnd, ControlIdToMenuHandle(kVolumeValueLabelId), m_hInstance, NULL);
        m_hEnabledToggle = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_CHECKBOX, 0, 0, 0, 0, hWnd, ControlIdToMenuHandle(kEnabledToggleId), m_hInstance, NULL);
        m_hTrackbar = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_NOTICKS | TBS_TRANSPARENTBKGND | TBS_FIXEDLENGTH, 0, 0, 0, 0, hWnd, ControlIdToMenuHandle(kTrackbarId), m_hInstance, NULL);
        m_hGithubLink = CreateWindowExW(0, L"STATIC", L"GitHub", WS_CHILD | WS_VISIBLE | SS_NOTIFY, 0, 0, 0, 0, hWnd, ControlIdToMenuHandle(kGithubLinkId), m_hInstance, NULL);
        m_hVersionLabel = CreateWindowExW(0, L"STATIC", L"Версия 0.1.5", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0, hWnd, ControlIdToMenuHandle(kVersionLabelId), m_hInstance, NULL);

        if (m_hTrackbar) {
            SendMessageW(m_hTrackbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(m_hTrackbar, TBM_SETPAGESIZE, 0, 10);
            SendMessageW(m_hTrackbar, TBM_SETLINESIZE, 0, 1);
            SendMessageW(m_hTrackbar, TBM_SETTHUMBLENGTH, Scale(kTrackThumbDiameter), 0);
            SubclassTrackbar();
        }

        ApplyFontsToControls();
        ApplyThemeToControls();
        LayoutControls();
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
        int trackbarPosition = VolumeToPercent(m_configManager.GetConfig().microphoneVolume);
        if (m_hTrackbar) {
            SendMessageW(m_hTrackbar, TBM_SETPOS, FALSE, trackbarPosition);
            InvalidateRect(m_hTrackbar, nullptr, FALSE);
        }
        UpdateVolumeLabel(trackbarPosition);
        UpdateEnabledToggleFromConfig();
    }

    void UpdateEnabledToggleFromConfig() {
        if (!m_hEnabledToggle) {
            return;
        }

        SendMessageW(m_hEnabledToggle, BM_SETCHECK, m_configManager.GetConfig().enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        InvalidateRect(m_hEnabledToggle, nullptr, FALSE);
    }

    void HandleEnabledToggleClicked() {
        if (!m_hEnabledToggle) {
            return;
        }

        bool enabled = SendMessageW(m_hEnabledToggle, BM_GETCHECK, 0, 0) != BST_CHECKED;
        SendMessageW(m_hEnabledToggle, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        m_configManager.SetEnabled(enabled);
        ApplyConfig(m_configManager.GetConfig(), false);
        InvalidateRect(m_hEnabledToggle, nullptr, FALSE);
    }

    void UpdateVolumeLabel(int volumePercent) {
        std::wstring labelText = MakeVolumePercentText(volumePercent);
        SetWindowTextW(m_hVolumeValueLabel, labelText.c_str());
    }

    void HandleTrackbarChanged() {
        if (!m_hTrackbar) {
            return;
        }

        int volumePercent = ClampPercent(static_cast<int>(SendMessageW(m_hTrackbar, TBM_GETPOS, 0, 0)));
        float volume = static_cast<float>(volumePercent) / 100.0f;
        m_configManager.SetMicrophoneVolume(volume);
        m_audioManager.ApplyVolumeImmediately(volume);
        UpdateVolumeLabel(volumePercent);
        InvalidateRect(m_hTrackbar, nullptr, FALSE);
    }

    void ShowMainWindow() {
        if (!m_hWnd) {
            return;
        }

        ShowWindow(m_hWnd, SW_SHOWNORMAL);
        SetForegroundWindow(m_hWnd);
    }

    void HandleDpiChanged(WPARAM wParam, LPARAM lParam) {
        m_dpi = HIWORD(wParam);
        RECT* suggestedRectangle = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(
            m_hWnd,
            NULL,
            suggestedRectangle->left,
            suggestedRectangle->top,
            suggestedRectangle->right - suggestedRectangle->left,
            suggestedRectangle->bottom - suggestedRectangle->top,
            SWP_NOZORDER | SWP_NOACTIVATE
        );

        UpdateWindowIcons();
        RecreateFonts();
        if (m_hTrackbar) {
            SendMessageW(m_hTrackbar, TBM_SETTHUMBLENGTH, Scale(kTrackThumbDiameter), 0);
        }
        LayoutControls();
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }

    void HandleGetMinMaxInfo(LPARAM lParam) {
        MINMAXINFO* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
        DWORD windowStyle = static_cast<DWORD>(GetWindowLongPtrW(m_hWnd, GWL_STYLE));
        DWORD windowExStyle = static_cast<DWORD>(GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE));
        RECT minimumRectangle = { 0, 0, Scale(kMinimumWindowClientWidth), Scale(kMinimumWindowClientHeight) };
        AdjustWindowRectExForDpi(&minimumRectangle, windowStyle, FALSE, windowExStyle, m_dpi);
        minMaxInfo->ptMinTrackSize.x = minimumRectangle.right - minimumRectangle.left;
        minMaxInfo->ptMinTrackSize.y = minimumRectangle.bottom - minimumRectangle.top;
    }

    static LRESULT CALLBACK TrackbarProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        Application* app = reinterpret_cast<Application*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (!app || !app->m_originalTrackbarProc) {
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
        }

        switch (uMsg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            {
                PAINTSTRUCT paintStruct = {};
                HDC deviceContext = BeginPaint(hWnd, &paintStruct);
                app->PaintTrackbar(deviceContext);
                EndPaint(hWnd, &paintStruct);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (app->m_trackbarDragging) {
                app->SetTrackbarPositionFromPoint(lParam, false);
            }
            return 0;
        case WM_LBUTTONDOWN:
            SetFocus(hWnd);
            SetCapture(hWnd);
            app->m_trackbarDragging = true;
            app->SetTrackbarPositionFromPoint(lParam, false);
            return 0;
        case WM_LBUTTONUP:
            if (app->m_trackbarDragging) {
                app->SetTrackbarPositionFromPoint(lParam, true);
                app->m_trackbarDragging = false;
                ReleaseCapture();
            }
            return 0;
        case WM_KEYDOWN:
        case WM_MOUSEWHEEL:
            {
                LRESULT result = CallWindowProcW(app->m_originalTrackbarProc, hWnd, uMsg, wParam, lParam);
                app->HandleTrackbarChanged();
                return result;
            }
        case WM_KEYUP:
            return CallWindowProcW(app->m_originalTrackbarProc, hWnd, uMsg, wParam, lParam);
        }

        return CallWindowProcW(app->m_originalTrackbarProc, hWnd, uMsg, wParam, lParam);
    }

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        Application* app = nullptr;
        if (uMsg == WM_NCCREATE) {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            app = reinterpret_cast<Application*>(cs->lpCreateParams);
            app->m_hWnd = hWnd;
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
            case WM_SIZE:
                app->LayoutControls();
                return 0;
            case WM_GETMINMAXINFO:
                app->HandleGetMinMaxInfo(lParam);
                return 0;
            case WM_HSCROLL:
                if (reinterpret_cast<HWND>(lParam) == app->m_hTrackbar) {
                    app->HandleTrackbarChanged();
                    return 0;
                }
                break;
            case WM_COMMAND:
                if (LOWORD(wParam) == kEnabledToggleId && HIWORD(wParam) == BN_CLICKED) {
                    app->HandleEnabledToggleClicked();
                    return 0;
                }
                if (LOWORD(wParam) == kGithubLinkId && HIWORD(wParam) == STN_CLICKED) {
                    ShellExecuteW(hWnd, L"open", kGithubReleasesUrl, NULL, NULL, SW_SHOWNORMAL);
                    return 0;
                }
                break;
            case WM_SETCURSOR:
                if (reinterpret_cast<HWND>(wParam) == app->m_hGithubLink) {
                    SetCursor(LoadCursorW(NULL, IDC_HAND));
                    return TRUE;
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
            case WM_DRAWITEM:
                if (wParam == kEnabledToggleId) {
                    app->DrawEnabledToggle(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
                    return TRUE;
                }
                break;
            case WM_CTLCOLORSTATIC:
                return app->HandleControlColor(reinterpret_cast<HWND>(lParam), reinterpret_cast<HDC>(wParam));
            case WM_DPICHANGED:
                app->HandleDpiChanged(wParam, lParam);
                return 0;
            case WM_SETTINGCHANGE:
                if (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
                    app->RefreshTheme();
                    return 0;
                }
                break;
            case WM_THEMECHANGED:
                app->RefreshTheme();
                return 0;
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
                if (app->m_trackbarDragging) {
                    app->m_trackbarDragging = false;
                    ReleaseCapture();
                }
                app->RestoreTrackbarSubclass();
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
    UINT m_dpi = USER_DEFAULT_SCREEN_DPI;
    ThemeColors m_themeColors = GetThemeColors(false);
    HBRUSH m_hBackgroundBrush = NULL;
    HFONT m_hTitleFont = NULL;
    HFONT m_hTextFont = NULL;
    HFONT m_hSecondaryFont = NULL;
    HFONT m_hLinkFont = NULL;
    HWND m_hWnd = NULL;
    HWND m_hTitleLabel = NULL;
    HWND m_hDescriptionLabel = NULL;
    HWND m_hVolumeCaptionLabel = NULL;
    HWND m_hVolumeValueLabel = NULL;
    HWND m_hEnabledToggle = NULL;
    HWND m_hTrackbar = NULL;
    HWND m_hVersionLabel = NULL;
    HWND m_hGithubLink = NULL;
    WNDPROC m_originalTrackbarProc = nullptr;
    HICON m_hSmallIcon = NULL;
    HICON m_hBigIcon = NULL;
    bool m_darkModeEnabled = false;
    bool m_trackbarDragging = false;
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

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        CoUninitialize();
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return 0;
    }

    RegisterAutostart();

    {
        Application app(hInstance);
        if (app.Initialize()) {
            app.Run();
        }
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);

    return 0;
}
