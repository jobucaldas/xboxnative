#pragma once
#include <windows.h>
#include <cstring>
#include <cstdint>
#include "wave.h"

// Wine 10 dlls/winmm/time.c and winmm.spec: DWORD stdcall timeGetTime().
// QPC is available to AppContainer. Divide before multiplying to avoid
// signed overflow after long uptimes; DWORD intentionally wraps at 2^32 ms.
namespace winmm_core {
inline DWORD milliseconds(ULONGLONG ticks, ULONGLONG frequency) {
    return DWORD((ticks / frequency) * 1000 +
                 ((ticks % frequency) * 1000) / frequency);
}
inline DWORD WINAPI get_time() {
    LARGE_INTEGER ticks{}, frequency{};
    QueryPerformanceCounter(&ticks);
    QueryPerformanceFrequency(&frequency);
    return milliseconds(ULONGLONG(ticks.QuadPart), ULONGLONG(frequency.QuadPart));
}
inline uintptr_t resolve(const char* dll, const char* name, WindowValidator validator = nullptr) {
    if (!dll || !name || _stricmp(dll, "winmm.dll")) return 0;
    window_validator = validator;
    if (!strcmp(name, "waveInOpen") || !strcmp(name, "waveOutOpen"))
        return reinterpret_cast<uintptr_t>(&wave_open);
    if (!strcmp(name, "timeGetTime")) return reinterpret_cast<uintptr_t>(&get_time);
    if (!strcmp(name, "waveOutGetNumDevs") || !strcmp(name, "waveInGetNumDevs"))
        return reinterpret_cast<uintptr_t>(&wave_device_count);
    if (!strcmp(name, "waveOutGetDevCapsA")) return reinterpret_cast<uintptr_t>(&wave_out_caps_a);
    if (!strcmp(name, "waveOutGetDevCapsW")) return reinterpret_cast<uintptr_t>(&wave_out_caps_w);
    if (!strcmp(name, "waveInGetDevCapsA")) return reinterpret_cast<uintptr_t>(&wave_in_caps_a);
    if (!strcmp(name, "waveInGetDevCapsW")) return reinterpret_cast<uintptr_t>(&wave_in_caps_w);
    if (!strcmp(name, "waveInClose") || !strcmp(name, "waveOutClose") ||
        !strcmp(name, "waveInReset") || !strcmp(name, "waveOutReset") ||
        !strcmp(name, "waveInStart"))
        return reinterpret_cast<uintptr_t>(&wave_invalid_handle);
    if (!strcmp(name, "waveInPrepareHeader") || !strcmp(name, "waveOutPrepareHeader"))
        return reinterpret_cast<uintptr_t>(&wave_prepare);
    if (!strcmp(name, "waveInUnprepareHeader") || !strcmp(name, "waveOutUnprepareHeader"))
        return reinterpret_cast<uintptr_t>(&wave_unprepare);
    if (!strcmp(name, "waveInAddBuffer")) return reinterpret_cast<uintptr_t>(&wave_add_buffer);
    if (!strcmp(name, "waveOutWrite")) return reinterpret_cast<uintptr_t>(&wave_write);
    if (!strcmp(name, "waveOutGetPosition")) return reinterpret_cast<uintptr_t>(&wave_position);
    // Period requests stay unresolved: Wine's no-op depends on its Unix wait
    // backend's sub-ms timing. That assumption does not hold on Xbox/UWP.
    return 0;
}
}
