#pragma once
#include "keyboard_feed.h"
#include "pointer_buttons.h"
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.UI.Input.h>

namespace user32_core {
// Own this adapter for at least the lifetime of the attached CoreWindow.
// LoadPackagedLibrary uses the same module identity as route.h's USER32 routes.
class CoreWindowKeyboard {
    KeyboardFeed feed_;
    PointerButtons buttons_;
    using PositionSink = void (WINAPI*)(double, double, double, double);
    PositionSink position_ = nullptr;
public:
    bool attach(winrt::Windows::UI::Core::CoreWindow const& window) {
        HMODULE module = LoadPackagedLibrary(L"uwph-u32c.dll", 0);
        auto sink = module ? reinterpret_cast<KeyboardFeed::Sink>(
            GetProcAddress(module, "uwph_u32c_key_event")) : nullptr;
        if (!sink) return false;
        position_ = reinterpret_cast<PositionSink>(GetProcAddress(module, "uwph_u32c_pointer_position"));
        // Retain the module: guest routes and registered callbacks use its state.
        feed_ = KeyboardFeed(sink);
        buttons_ = PointerButtons(sink);
        auto pointer = [this](auto&& sender, auto&& args) {
            auto point = args.CurrentPoint();
            // Touch and pen contacts must not overwrite physical mouse state.
            if (point.PointerDevice().PointerDeviceType() !=
                winrt::Windows::Devices::Input::PointerDeviceType::Mouse) return;
            if (position_) {
                auto p = point.Position();
                auto bounds = sender.Bounds();
                position_(p.X, p.Y, bounds.Width, bounds.Height);
            }
            auto p = point.Properties();
            buttons_.snapshot(p.IsLeftButtonPressed(), p.IsRightButtonPressed(),
                              p.IsMiddleButtonPressed(), p.IsXButton1Pressed(),
                              p.IsXButton2Pressed());
        };
        window.PointerPressed(pointer);
        window.PointerReleased(pointer);
        window.PointerMoved(pointer);
        window.PointerCaptureLost([this](auto&&, auto&& args) {
            if (args.CurrentPoint().PointerDevice().PointerDeviceType() ==
                winrt::Windows::Devices::Input::PointerDeviceType::Mouse)
                buttons_.release_all();
        });
        window.KeyDown([this](auto&&, auto&& args) {
            auto status = args.KeyStatus();
            feed_.key(static_cast<unsigned>(args.VirtualKey()), status.ScanCode,
                      status.IsExtendedKey, true);
        });
        window.KeyUp([this](auto&&, auto&& args) {
            auto status = args.KeyStatus();
            feed_.key(static_cast<unsigned>(args.VirtualKey()), status.ScanCode,
                      status.IsExtendedKey, false);
        });
        window.Activated([this](auto&&, auto&& args) {
            if (args.WindowActivationState() ==
                winrt::Windows::UI::Core::CoreWindowActivationState::Deactivated)
            {
                feed_.release_all();
                buttons_.release_all();
            }
        });
        window.Closed([this](auto&&, auto&&) { feed_.release_all(); buttons_.release_all(); });
        return true;
    }
};
}
