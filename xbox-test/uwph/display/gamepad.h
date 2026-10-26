// Shared WGI -> XInput conversion; no dead zone (XInput callers apply their own).
#pragma once
#include <math.h>
static double bounded(double value,double lo,double hi) {
    // Invalid sensor values must not reach an out-of-range floating-to-int cast.
    return !isfinite(value)?0:value<lo?lo:value>hi?hi:value;
}
static short axis(double v) { v=bounded(v,-1,1);return (short)(v<0?v*32768:v*32767); }
static XINPUT_GAMEPAD convert_pad(winrt::Windows::Gaming::Input::GamepadReading const& r) {
    using namespace winrt::Windows::Gaming::Input;
    XINPUT_GAMEPAD p{};
    struct B { GamepadButtons w; WORD x; };
    const B bits[]={{GamepadButtons::A,XINPUT_GAMEPAD_A},{GamepadButtons::B,XINPUT_GAMEPAD_B},
    {GamepadButtons::X,XINPUT_GAMEPAD_X},{GamepadButtons::Y,XINPUT_GAMEPAD_Y},
    {GamepadButtons::DPadUp,XINPUT_GAMEPAD_DPAD_UP},{GamepadButtons::DPadDown,XINPUT_GAMEPAD_DPAD_DOWN},
    {GamepadButtons::DPadLeft,XINPUT_GAMEPAD_DPAD_LEFT},{GamepadButtons::DPadRight,XINPUT_GAMEPAD_DPAD_RIGHT},
    {GamepadButtons::Menu,XINPUT_GAMEPAD_START},{GamepadButtons::View,XINPUT_GAMEPAD_BACK},
    {GamepadButtons::LeftShoulder,XINPUT_GAMEPAD_LEFT_SHOULDER},{GamepadButtons::RightShoulder,XINPUT_GAMEPAD_RIGHT_SHOULDER},
    {GamepadButtons::LeftThumbstick,XINPUT_GAMEPAD_LEFT_THUMB},{GamepadButtons::RightThumbstick,XINPUT_GAMEPAD_RIGHT_THUMB}};
    for(auto b:bits) if((r.Buttons&b.w)!=GamepadButtons::None)p.wButtons|=b.x;
    p.bLeftTrigger=(BYTE)(bounded(r.LeftTrigger,0,1)*255);p.bRightTrigger=(BYTE)(bounded(r.RightTrigger,0,1)*255);
    p.sThumbLX=axis(r.LeftThumbstickX);p.sThumbLY=axis(r.LeftThumbstickY);
    p.sThumbRX=axis(r.RightThumbstickX);p.sThumbRY=axis(r.RightThumbstickY);return p;
}
