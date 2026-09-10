#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "App.g.cpp"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::MicGainControl::implementation
{
    App::App()
    {
#if defined(DEBUG) || defined(_DEBUG)
        UnhandledException([this](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                OutputDebugStringW(errorMessage.c_str());
            }
        });
#endif
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& args)
    {
        m_window = winrt::make<MicGainControl::implementation::MainWindow>();
        m_window.Activate();
    }
}
