#pragma once

#include "App.g.h"

namespace winrt::MicGainControl::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

    private:
        winrt::Microsoft::UI::Xaml::Window m_window{ nullptr };
    };
}

namespace winrt::MicGainControl::factory_implementation
{
    struct App : AppT<App, implementation::App>
    {
    };
}
