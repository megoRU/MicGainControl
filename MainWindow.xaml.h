#pragma once

#include "MainWindow.g.h"
#include "ConfigManager.hpp"
#include "AudioManager.hpp"
#include "TrayManager.hpp"

namespace winrt::MicGainControl::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        ~MainWindow();

        void VolumeSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& e);
        void EnabledToggle_Toggled(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        void ShowTrayMenu();
        void ShowMainWindow();
        void HideMainWindow();
        void ExitApp();

    private:
        HWND GetHwnd();
        void ApplyConfig(const Config& cfg, bool updateUI);

        ConfigManager m_configManager;
        AudioManager m_audioManager;
        TrayManager m_trayManager{ GetModuleHandle(nullptr) };
        HWND m_hWnd{ nullptr };
        bool m_allowExit{ false };
        bool m_updatingUI{ false };
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
    };
}

namespace winrt::MicGainControl::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
