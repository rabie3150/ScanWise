#pragma once

#include <memory>
#include <winrt/Microsoft.UI.Xaml.h>

namespace scanwise
{
    struct App : winrt::Microsoft::UI::Xaml::ApplicationT<App>
    {
        struct ui_state;
        void OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
    };
}
