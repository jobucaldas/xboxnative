// SPDX-License-Identifier: GPL-3.0-or-later
// joystick.h and its winmm routes against the SDK's own joystickapi.h prototypes
// and mmsyscom.h values (off-device, Wine). Compiled with the desktop headers, so
// every signature is compared with the declaration a Windows guest links against.
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <type_traits>
#include "joystick.h"
#include "routes.h"
static_assert(std::is_same<decltype(&joy_routes::joyGetNumDevs), decltype(&joyGetNumDevs)>::value, "joyGetNumDevs");
static_assert(std::is_same<decltype(&joy_routes::joyGetPos), decltype(&joyGetPos)>::value, "joyGetPos");
static_assert(std::is_same<decltype(&joy_routes::joyGetPosEx), decltype(&joyGetPosEx)>::value, "joyGetPosEx");
static_assert(std::is_same<decltype(&joy_routes::joyGetThreshold), decltype(&joyGetThreshold)>::value, "joyGetThreshold");
static_assert(std::is_same<decltype(&joy_routes::joySetThreshold), decltype(&joySetThreshold)>::value, "joySetThreshold");
static_assert(std::is_same<decltype(&joy_routes::joySetCapture), decltype(&joySetCapture)>::value, "joySetCapture");
static_assert(std::is_same<decltype(&joy_routes::joyReleaseCapture), decltype(&joyReleaseCapture)>::value, "joyReleaseCapture");
static_assert(std::is_same<decltype(&joy_routes::joyGetDevCapsA), decltype(&joyGetDevCapsA)>::value, "joyGetDevCapsA");
static_assert(std::is_same<decltype(&joy_routes::joyGetDevCapsW), decltype(&joyGetDevCapsW)>::value, "joyGetDevCapsW");
static_assert(std::is_same<decltype(&joy_routes::joyConfigChanged), decltype(&joyConfigChanged)>::value, "joyConfigChanged");
static_assert(joy_routes::mmsyserr_nodriver == MMSYSERR_NODRIVER && joy_routes::mmsyserr_invalparam == MMSYSERR_INVALPARAM, "MMSYSERR values");
static_assert(joy_routes::joyerr_parms == JOYERR_PARMS, "JOYERR_PARMS value");
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
int main() {
    const char* names[] = {"joyConfigChanged", "joyGetDevCapsA", "joyGetDevCapsW", "joyGetNumDevs", "joyGetPos",
                           "joyGetPosEx", "joyGetThreshold", "joyReleaseCapture", "joySetCapture", "joySetThreshold"};
    for (auto name : names) CHECK(joy_routes::resolve(name) != 0);
    CHECK(joy_routes::resolve("joyNotAnExport") == 0);
    CHECK(joy_routes::resolve("joyGetNumDevs") == reinterpret_cast<uintptr_t>(&joy_routes::joyGetNumDevs));
    // winmm routes: joystick names resolve for winmm.dll only.
    CHECK(audio_routes::resolve("winmm.dll", "joyGetPosEx") == reinterpret_cast<uintptr_t>(&joy_routes::joyGetPosEx));
    CHECK(audio_routes::resolve("user32.dll", "joyGetPosEx") == 0);
    // No joystick driver: zero devices, and every device id is invalid.
    CHECK(joy_routes::joyGetNumDevs() == 0);
    JOYINFO ji{};
    ji.wXpos = 0x1234;
    CHECK(joy_routes::joyGetPos(JOYSTICKID1, &ji) == JOYERR_PARMS && ji.wXpos == 0x1234);
    JOYINFOEX jex{};
    jex.dwSize = sizeof jex;
    jex.dwXpos = 0x1234;
    CHECK(joy_routes::joyGetPosEx(JOYSTICKID1, &jex) == JOYERR_PARMS && jex.dwXpos == 0x1234);
    CHECK(joy_routes::joyGetPosEx(15, &jex) == JOYERR_PARMS);
    UINT threshold = 7;
    CHECK(joy_routes::joyGetThreshold(JOYSTICKID1, &threshold) == JOYERR_PARMS && threshold == 7);
    CHECK(joy_routes::joySetThreshold(JOYSTICKID1, 5) == JOYERR_PARMS);
    CHECK(joy_routes::joySetCapture(nullptr, JOYSTICKID1, 10, TRUE) == JOYERR_PARMS);
    CHECK(joy_routes::joyReleaseCapture(JOYSTICKID1) == JOYERR_PARMS);
    // Device caps: no driver (MMSYSERR_NODRIVER); a NULL buffer is an invalid parameter. Nothing written.
    JOYCAPSW capsW{};
    capsW.wMid = 0x4321;
    CHECK(joy_routes::joyGetDevCapsW(JOYSTICKID1, &capsW, sizeof capsW) == MMSYSERR_NODRIVER && capsW.wMid == 0x4321);
    CHECK(joy_routes::joyGetDevCapsW(JOYSTICKID1, nullptr, sizeof capsW) == MMSYSERR_INVALPARAM);
    JOYCAPSA capsA{};
    capsA.wMid = 0x4321;
    CHECK(joy_routes::joyGetDevCapsA(JOYSTICKID1, &capsA, sizeof capsA) == MMSYSERR_NODRIVER && capsA.wMid == 0x4321);
    CHECK(joy_routes::joyConfigChanged(0) == MMSYSERR_NODRIVER);
    if (failures) { std::printf("JOY TEST FAIL checks=%d failures=%d\n", checks, failures); return 1; }
    std::printf("JOY TEST PASS checks=%d\n", checks);
    return 0;
}
