#include <windows.h>
#include <xinput.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <cstdio>
#include <limits>
#include "../display/gamepad.h"
#define CHECK(x) do { if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;} } while(0)
int main() {
    using namespace winrt::Windows::Gaming::Input;
    GamepadReading r{};
    r.Buttons=GamepadButtons::A|GamepadButtons::DPadUp|GamepadButtons::Menu|GamepadButtons::LeftThumbstick;
    r.LeftThumbstickX=-1;r.LeftThumbstickY=1;r.RightThumbstickX=-2;r.RightThumbstickY=2;
    r.LeftTrigger=0.5;r.RightTrigger=1;
    auto p=convert_pad(r);
    CHECK(p.wButtons==(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_LEFT_THUMB));
    CHECK(p.sThumbLX==-32768 && p.sThumbLY==32767 && p.sThumbRX==-32768 && p.sThumbRY==32767);
    CHECK(p.bLeftTrigger==127 && p.bRightTrigger==255);
    r.LeftThumbstickX=std::numeric_limits<double>::quiet_NaN();
    r.LeftThumbstickY=std::numeric_limits<double>::infinity();
    r.LeftTrigger=-1;r.RightTrigger=2;
    p=convert_pad(r);
    CHECK(!p.sThumbLX && !p.sThumbLY && !p.bLeftTrigger && p.bRightTrigger==255);
    r={};p=convert_pad(r);
    CHECK(!p.wButtons && !p.sThumbLX && !p.sThumbLY && !p.sThumbRX && !p.sThumbRY && !p.bLeftTrigger && !p.bRightTrigger);
    puts("WGI conversion: buttons, axis endpoints, trigger ranges and nonfinite rejection PASS");
}
