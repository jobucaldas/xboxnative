// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// winmm joystick API (joystickapi.h) for guests. This console has no legacy
// joystick driver: a pad reaches games through XInput and HID (uwph/xinput,
// uwph/hid). The family therefore answers as Windows does with no joystick driver:
// joyGetNumDevs reports zero supported devices, so every device id is invalid
// (JOYERR_PARMS), joyGetDevCaps reports MMSYSERR_NODRIVER, joyConfigChanged reports
// MMSYSERR_NODRIVER, and no output buffer is written. Nothing here fabricates stick,
// button or capture state. Values are mirrored from mmsyscom.h and joystickapi.h
// (desktop partition, hidden by the UWP family); joystick-test.cpp checks them
// against the SDK headers.
#include <windows.h>
#include <cstdint>
#include <cstring>
// Global tags, so the parameter types are the SDK's own structs (same tags).
struct joyinfo_tag;
struct joyinfoex_tag;
struct tagJOYCAPSA;
struct tagJOYCAPSW;
namespace joy_routes {
constexpr UINT mmsyserr_nodriver=6;   // MMSYSERR_NODRIVER (MMSYSERR_BASE+6)
constexpr UINT mmsyserr_invalparam=11; // MMSYSERR_INVALPARAM (MMSYSERR_BASE+11)
constexpr UINT joyerr_parms=165;      // JOYERR_PARMS (JOYERR_BASE+5)
constexpr UINT supported_devices=0;   // no joystick driver: no device id is valid
inline UINT WINAPI joyGetNumDevs(void){return supported_devices;}
inline UINT WINAPI joyGetPos(UINT,struct joyinfo_tag*){return joyerr_parms;}
inline UINT WINAPI joyGetPosEx(UINT,struct joyinfoex_tag*){return joyerr_parms;}
inline UINT WINAPI joyGetThreshold(UINT,UINT*){return joyerr_parms;}
inline UINT WINAPI joySetThreshold(UINT,UINT){return joyerr_parms;}
inline UINT WINAPI joySetCapture(HWND,UINT,UINT,BOOL){return joyerr_parms;}
inline UINT WINAPI joyReleaseCapture(UINT){return joyerr_parms;}
inline UINT WINAPI joyGetDevCapsA(UINT_PTR,struct tagJOYCAPSA* caps,UINT){return caps?mmsyserr_nodriver:mmsyserr_invalparam;}
inline UINT WINAPI joyGetDevCapsW(UINT_PTR,struct tagJOYCAPSW* caps,UINT){return caps?mmsyserr_nodriver:mmsyserr_invalparam;}
inline UINT WINAPI joyConfigChanged(DWORD){return mmsyserr_nodriver;}
// Name -> address for the winmm route table (audio/routes.h). Zero: not a joystick export.
inline uintptr_t resolve(const char* name){
 struct Entry {const char* name;uintptr_t address;};
 static const Entry table[]={
  {"joyConfigChanged",uintptr_t(&joyConfigChanged)},
  {"joyGetDevCapsA",uintptr_t(&joyGetDevCapsA)},
  {"joyGetDevCapsW",uintptr_t(&joyGetDevCapsW)},
  {"joyGetNumDevs",uintptr_t(&joyGetNumDevs)},
  {"joyGetPos",uintptr_t(&joyGetPos)},
  {"joyGetPosEx",uintptr_t(&joyGetPosEx)},
  {"joyGetThreshold",uintptr_t(&joyGetThreshold)},
  {"joyReleaseCapture",uintptr_t(&joyReleaseCapture)},
  {"joySetCapture",uintptr_t(&joySetCapture)},
  {"joySetThreshold",uintptr_t(&joySetThreshold)},
 };
 for(const auto& e:table) if(!strcmp(e.name,name)) return e.address;
 return 0;
}
}
