#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::MicGainControl::implementation
{
    App::App()
    {
        // Загружает App.xaml вместе с XamlControlsResources — без этого
        // не работают стандартные стили и ThemeResource'ы WinUI.
        InitializeComponent();

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
        // Приложение живёт в трее и прописано в автозапуск, поэтому окно создаётся,
        // но не показывается при старте (Win32-версия делала ShowWindow(SW_HIDE)).
        // Окно открывается двойным кликом по значку в трее или пунктом «Открыть окно».
        m_window = winrt::make<MicGainControl::implementation::MainWindow>();
    }
}
