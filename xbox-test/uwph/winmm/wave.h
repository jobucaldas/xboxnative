#pragma once
#include <windows.h>
#include <cstddef>
#include <atomic>

// There is no wave driver in this backend. Match the failed driver-init
// branches of Wine 10 waveform.c, including ANSI null-pointer precedence.
// Do not forward guest device IDs or handles to host multimedia services.
namespace winmm_core {
// The UWP SDK hides desktop wave structures and multimedia error macros.
// Capability buffers are opaque: this backend never reads or writes them.
// Values from mmsystem.h: MMSYSERR_NODRIVER=6, MMSYSERR_INVALPARAM=11.
constexpr UINT wave_no_driver = 6, wave_invalid_param = 11;
// Open routes cannot succeed, so no guest wave handle can be valid. Wine's
// close/reset/start paths validate the handle before touching device state.
// MMSYSERR_INVALHANDLE=5. Replace together with any future handle backend.
inline UINT WINAPI wave_invalid_handle(HANDLE) { return 5; }
// Guest window validation must use the guest USER32 registry, never host HWNDs.
using WindowValidator = BOOL (WINAPI*)(HWND);
inline std::atomic<WindowValidator> window_validator{nullptr};
// Wine 10 waveform.c open paths: output-pointer and callback validation precede
// driver startup. No driver exists here, so format/device data stay untouched.
inline UINT WINAPI wave_open(HANDLE* output, UINT, const void*, DWORD_PTR callback,
                             DWORD_PTR, DWORD flags) {
    if (!output && !(flags & 1)) return 11; // WAVE_FORMAT_QUERY
    auto validate = window_validator.load();
    if ((flags & 0x70000) == 0x10000 && callback &&
        (!validate || !validate(reinterpret_cast<HWND>(callback))))
        return 11; // CALLBACK_WINDOW, MMSYSERR_INVALPARAM
    return 6; // MMSYSERR_NODRIVER
}
inline UINT WINAPI wave_device_count() { return 0; }
inline UINT WINAPI wave_out_caps_a(UINT_PTR, void* caps, UINT) {
    return caps ? wave_no_driver : wave_invalid_param;
}
inline UINT WINAPI wave_out_caps_w(UINT_PTR, void*, UINT) {
    return wave_no_driver;
}
inline UINT WINAPI wave_in_caps_a(UINT_PTR, void* caps, UINT) {
    return caps ? wave_no_driver : wave_invalid_param;
}
inline UINT WINAPI wave_in_caps_w(UINT_PTR, void*, UINT) {
    return wave_no_driver;
}
// ABI mirror of x64 WAVEHDR; the AppContainer SDK hides its declaration.
struct WaveHeader {
    char* data;
    DWORD length, recorded;
    DWORD_PTR user;
    DWORD flags, loops;
    WaveHeader* next;
    DWORD_PTR reserved;
};
static_assert(sizeof(WaveHeader) == 48, "x64 WAVEHDR ABI");
static_assert(offsetof(WaveHeader, flags) == 24, "WAVEHDR flags ABI");
// Wine waveform.c checks buffer state before handle validity in these APIs.
inline UINT WINAPI wave_prepare(HANDLE, WaveHeader* header, UINT size) {
    if (!header || size < sizeof(WaveHeader)) return 11;
    return (header->flags & 2) ? 0 : 5; // WHDR_PREPARED
}
inline UINT WINAPI wave_unprepare(HANDLE, WaveHeader* header, UINT size) {
    if (!header || size < sizeof(WaveHeader)) return 11;
    if (header->flags & 16) return 33; // WHDR_INQUEUE, WAVERR_STILLPLAYING
    return (header->flags & 2) ? 5 : 0;
}
inline UINT WINAPI wave_add_buffer(HANDLE, WaveHeader* header, UINT size) {
    if (!header || size < sizeof(WaveHeader)) return 11;
    return (header->flags & 2) ? 5 : 34; // WAVERR_UNPREPARED
}
inline UINT WINAPI wave_write(HANDLE, void*, UINT) { return 5; }
inline UINT WINAPI wave_position(HANDLE, void* time, UINT size) {
    if (!time || !size) return 11;
    if (size < 12) return 1; // sizeof(MMTIME), MMSYSERR_ERROR
    return 5;
}
}
