// Reuse the exact Play loader and its existing completion assertions.
#define WinMain playbackMain
#include "../../app/tests/play-runner.cpp"
#undef WinMain
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR,int show) {
    XINPUT_GAMEPAD pad{};pad.wButtons=XINPUT_GAMEPAD_A;pad.sThumbLY=-32768;
    xinput_core::publish(0,&pad);
    char mode[]="xinput";
    return playbackMain(instance,previous,mode,show);
}
