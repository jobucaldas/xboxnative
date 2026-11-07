// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the exact Play loader and its existing completion assertions.
#define WinMain playbackMain
#include "../../app/tests/play-runner.cpp"
#undef WinMain
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR,int show) {
    char mode[]="opengl32";
    return playbackMain(instance,previous,mode,show);
}
