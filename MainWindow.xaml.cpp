#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.g.cpp"

#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <commctrl.h>
#include <string>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace {
    constexpr UINT_PTR kSubclassId = 101;

    LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, [[maybe_unused]] UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
    {
        auto self = reinterpret_cast<winrt::MicGainControl::implementation::MainWindow*>(dwRefData);
        if (self)
        {
            if (uMsg == WM_TRAY_ICON)
            {
                if (LOWORD(lParam) == WM_RBUTTONUP)
                {
                    self->ShowTrayMenu();
                }
                else if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
                {
                    self->ShowMainWindow();
                }
                return 0;
            }
        }
        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }
}

namespace winrt::MicGainControl::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();

        m_dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();

        auto appWindow = this->AppWindow();
        appWindow.Title(L"MicGainControl");
        appWindow.Resize({ 480, 280 });

        HWND hwnd = GetHwnd();
        if (hwnd)
        {
            SetWindowSubclass(hwnd, SubclassProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this));
        }

        if (m_configManager.Load() && m_audioManager.Initialize())
        {
            ApplyConfig(m_configManager.GetConfig(), true);

            m_configManager.SetCallback([this](const Config& cfg) {
                if (m_dispatcher)
                {
                    m_dispatcher.TryEnqueue([this, cfg]() {
                        ApplyConfig(cfg, true);
                    });
                }
            });
            m_configManager.StartWatching();
        }

        m_trayManager.SetOnToggle([this]() {
            if (m_dispatcher)
            {
                m_dispatcher.TryEnqueue([this]() {
                    bool newState = !m_configManager.GetConfig().enabled;
                    m_configManager.SetEnabled(newState);
                    ApplyConfig(m_configManager.GetConfig(), true);
                });
            }
        });

        m_trayManager.SetOnOpenConfig([this]() {
            if (m_dispatcher)
            {
                m_dispatcher.TryEnqueue([this]() {
                    ShowMainWindow();
                });
            }
        });

        m_trayManager.SetOnExit([this]() {
            if (m_dispatcher)
            {
                m_dispatcher.TryEnqueue([this]() {
                    ExitApp();
                });
            }
        });

        if (hwnd)
        {
            m_trayManager.CreateTrayIcon(hwnd);
        }

        appWindow.Closing([this](auto const&, winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args) {
            if (!m_allowExit) {
                args.Cancel(true);
                HideMainWindow();
            }
        });

        m_updatingUI = false;
    }

    MainWindow::~MainWindow()
    {
        // Окно уже разрушается, поэтому берём кэшированный HWND, а не спрашиваем его заново.
        if (m_hWnd)
        {
            RemoveWindowSubclass(m_hWnd, SubclassProc, kSubclassId);
            m_hWnd = nullptr;
        }
    }

    HWND MainWindow::GetHwnd()
    {
        if (!m_hWnd)
        {
            auto windowNative = this->try_as<::IWindowNative>();
            if (windowNative)
            {
                windowNative->get_WindowHandle(&m_hWnd);
            }
        }
        return m_hWnd;
    }

    void MainWindow::ApplyConfig(const Config& cfg, bool updateUI)
    {
        m_audioManager.SetTargetVolume(cfg.microphoneVolume);
        m_audioManager.SetEnabled(cfg.enabled);
        m_trayManager.SetEnabledState(cfg.enabled);

        if (updateUI)
        {
            const bool wasUpdatingUI = m_updatingUI;
            m_updatingUI = true;
            int volPercent = static_cast<int>((cfg.microphoneVolume * 100.0f) + 0.5f);
            if (volPercent < 0) volPercent = 0;
            if (volPercent > 100) volPercent = 100;

            VolumeSlider().Value(volPercent);
            VolumeValueText().Text(std::to_wstring(volPercent) + L"%");
            VolumeSlider().IsEnabled(cfg.enabled);
            EnabledToggle().IsOn(cfg.enabled);
            m_updatingUI = wasUpdatingUI;
        }
    }

    void MainWindow::VolumeSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& e)
    {
        if (m_updatingUI) return;

        int volPercent = static_cast<int>(e.NewValue());
        float volume = static_cast<float>(volPercent) / 100.0f;
        VolumeValueText().Text(std::to_wstring(volPercent) + L"%");

        m_configManager.SetMicrophoneVolume(volume);
        m_audioManager.ApplyVolumeImmediately(volume);
    }

    void MainWindow::EnabledToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_updatingUI) return;

        bool enabled = EnabledToggle().IsOn();
        VolumeSlider().IsEnabled(enabled);

        m_configManager.SetEnabled(enabled);
        ApplyConfig(m_configManager.GetConfig(), false);
    }

    void MainWindow::ShowTrayMenu()
    {
        HWND hwnd = GetHwnd();
        if (hwnd)
        {
            m_trayManager.ShowContextMenu(hwnd);
        }
    }

    void MainWindow::ShowMainWindow()
    {
        this->AppWindow().Show();
        this->Activate();

        if (HWND hwnd = GetHwnd())
        {
            SetForegroundWindow(hwnd);
        }
    }

    void MainWindow::HideMainWindow()
    {
        this->AppWindow().Hide();
    }

    void MainWindow::ExitApp()
    {
        m_allowExit = true;
        m_trayManager.RemoveTrayIcon();
        this->Close();

        // Окно могло ни разу не показываться (старт в трее), поэтому закрытия
        // окна недостаточно — завершаем приложение явно.
        if (auto app = winrt::Microsoft::UI::Xaml::Application::Current())
        {
            app.Exit();
        }
    }
}
