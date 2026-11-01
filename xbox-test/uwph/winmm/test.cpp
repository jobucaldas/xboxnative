#include "route.h"
#include <mmsystem.h>
#include <cstdio>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    using namespace winmm_core;
    CHECK(milliseconds(123456789, 10000000) == 12345);
    CHECK(milliseconds(4294967296ULL * 10000, 10000000) == 0);
    CHECK(milliseconds(4294967297ULL * 10000, 10000000) == 1);
    CHECK(milliseconds(9000000000000000000ULL, 10000000) == DWORD(900000000000000ULL));
    CHECK(resolve("WINMM.DLL", "timeGetTime") != 0);
    CHECK(resolve("kernel32.dll", "timeGetTime") == 0);
    CHECK(resolve("winmm.dll", "timeBeginPeriod") == 0);
    CHECK(resolve("winmm.dll", nullptr) == 0);
    auto fn = reinterpret_cast<DWORD (WINAPI*)()>(resolve("winmm.dll", "timeGetTime"));
    DWORD before = fn();
    Sleep(35);
    DWORD elapsed = fn() - before;
    CHECK(elapsed >= 20 && elapsed < 2000);
    // No wave backend: counts agree with capability errors. Exercise the
    // resolved WINAPI functions with IDs, mapper IDs, sizes and null buffers.
    const char* counts[] = {"waveInGetNumDevs", "waveOutGetNumDevs"};
    for (auto name : counts) {
        auto count = reinterpret_cast<UINT (WINAPI*)()>(resolve("winmm.dll", name));
        CHECK(count && count() == 0);
    }
    const char* caps_names[] = {"waveInGetDevCapsA", "waveInGetDevCapsW",
                               "waveOutGetDevCapsA", "waveOutGetDevCapsW"};
    UINT_PTR ids[] = {0, 1, UINT_PTR(WAVE_MAPPER), UINT_PTR(-1), UINT_PTR(0x123456789ULL)};
    UINT sizes[] = {0, 1, sizeof(WAVEOUTCAPSW), 0xffffffff};
    for (auto name : caps_names) {
        auto caps = reinterpret_cast<UINT (WINAPI*)(UINT_PTR, void*, UINT)>(resolve("WINMM.DLL", name));
        CHECK(caps);
        for (auto id : ids) for (auto size : sizes) {
            unsigned char buffer[sizeof(WAVEOUTCAPSW) + 8];
            memset(buffer, 0xa5, sizeof(buffer));
            SetLastError(0xdead);
            CHECK(caps(id, buffer, size) == MMSYSERR_NODRIVER);
            CHECK(GetLastError() == 0xdead);
            for (auto value : buffer) CHECK(value == 0xa5);
            CHECK(caps(id, nullptr, size) ==
                (name[strlen(name)-1] == 'A' ? MMSYSERR_INVALPARAM : MMSYSERR_NODRIVER));
            CHECK(GetLastError() == 0xdead);
        }
    }
    const char* handle_names[] = {"waveInClose", "waveOutClose", "waveInReset",
                                  "waveOutReset", "waveInStart"};
    HMODULE oracle = LoadLibraryW(L"winmm.dll");
    CHECK(oracle);
    for (auto name : handle_names) {
        using Function = UINT (WINAPI*)(HANDLE);
        auto call = reinterpret_cast<Function>(resolve("winmm.dll", name));
        auto wine_call = reinterpret_cast<Function>(GetProcAddress(oracle, name));
        CHECK(call && wine_call);
        for (auto value : {UINT_PTR(0), UINT_PTR(1), UINT_PTR(-1), UINT_PTR(0x123456789ULL)}) {
            HANDLE handle = reinterpret_cast<HANDLE>(value);
            SetLastError(0xdead);
            CHECK(call(handle) == MMSYSERR_INVALHANDLE);
            CHECK(GetLastError() == 0xdead);
            CHECK(wine_call(handle) == MMSYSERR_INVALHANDLE);
            CHECK(GetLastError() == 0xdead);
        }
    }
    static_assert(sizeof(WaveHeader) == sizeof(WAVEHDR), "SDK header size");
    static_assert(offsetof(WaveHeader, flags) == offsetof(WAVEHDR, dwFlags), "SDK flags offset");
    CHECK(sizeof(MMTIME) == 12);
    const char* buffer_names[] = {"waveInPrepareHeader", "waveOutPrepareHeader",
        "waveInUnprepareHeader", "waveOutUnprepareHeader", "waveInAddBuffer",
        "waveOutWrite", "waveOutGetPosition"};
    for (auto name : buffer_names) {
        using Function = UINT (WINAPI*)(HANDLE, void*, UINT);
        auto call = reinterpret_cast<Function>(resolve("winmm.dll", name));
        auto wine_call = reinterpret_cast<Function>(GetProcAddress(oracle, name));
        CHECK(call && wine_call);
        for (auto value : {UINT_PTR(0), UINT_PTR(1), UINT_PTR(-1), UINT_PTR(0x123456789ULL)})
        for (DWORD flags : {0UL, 2UL, 16UL, 18UL, 0xffffffffUL})
        for (UINT size : {0U, 1U, 11U, 12U, 47U, 48U, 0xffffffffU})
        for (bool null_buffer : {false, true}) {
            WAVEHDR header{};
            header.dwFlags = flags;
            WAVEHDR expected = header;
            void* buffer = null_buffer ? nullptr : &header;
            SetLastError(0xdead);
            UINT result = call(reinterpret_cast<HANDLE>(value), buffer, size);
            CHECK(GetLastError() == 0xdead);
            CHECK(!memcmp(&header, &expected, sizeof(header)));
            CHECK(wine_call(reinterpret_cast<HANDLE>(value), buffer, size) == result);
            CHECK(GetLastError() == 0xdead);
            CHECK(!memcmp(&header, &expected, sizeof(header)));
        }
    }
    CHECK(!resolve("user32.dll", "waveInGetNumDevs"));
    CHECK(!resolve("winmm.dll", "waveingetnumdevs"));
    for (auto name : {"waveInOpen", "waveOutOpen"}) {
        using Open = UINT (WINAPI*)(HANDLE*, UINT, const void*, DWORD_PTR, DWORD_PTR, DWORD);
        auto call = reinterpret_cast<Open>(resolve("winmm.dll", name));
        auto wine_call = reinterpret_cast<Open>(GetProcAddress(GetModuleHandleW(L"winmm.dll"), name));
        CHECK(call && wine_call);
        for (DWORD flags : {0UL, 1UL, 0x10000UL, 0x10001UL, 0x30000UL, 0x50000UL, 0x70000UL})
        for (DWORD_PTR callback : {DWORD_PTR(0), DWORD_PTR(1)})
        for (bool null_output : {false, true}) {
            HANDLE output = reinterpret_cast<HANDLE>(UINT_PTR(0x1234));
            SetLastError(0xdead);
            UINT result = call(null_output ? nullptr : &output, WAVE_MAPPER,
                               nullptr, callback, 0, flags);
            CHECK(result == ((!null_output || (flags & 1)) &&
                !((flags & CALLBACK_TYPEMASK) == CALLBACK_WINDOW && callback)
                ? MMSYSERR_NODRIVER : MMSYSERR_INVALPARAM));
            CHECK(output == reinterpret_cast<HANDLE>(UINT_PTR(0x1234)));
            CHECK(GetLastError() == 0xdead);
            // Only pre-driver failures are differential: the oracle's audio
            // backend is environment-dependent and may initialize a driver.
            if (result == MMSYSERR_INVALPARAM) {
                CHECK(wine_call(null_output ? nullptr : &output, WAVE_MAPPER,
                                nullptr, callback, 0, flags) == result);
                CHECK(output == reinterpret_cast<HANDLE>(UINT_PTR(0x1234)));
            }
        }
        window_validator = [](HWND h) -> BOOL { return h == reinterpret_cast<HWND>(UINT_PTR(1)); };
        HANDLE output{};
        CHECK(call(&output, 0, nullptr, 1, 0, CALLBACK_WINDOW) == MMSYSERR_NODRIVER);
        CHECK(call(&output, 0, nullptr, 2, 0, CALLBACK_WINDOW) == MMSYSERR_INVALPARAM);
    }
    CHECK(!resolve("winmm.dll", "#176"));
    FreeLibrary(oracle);
    std::puts("WINMM OFFDEVICE PASS clock, wrap, long uptime, resolver, elapsed time, six wave discovery exports, errors, untouched buffers, five invalid-handle seven wave-buffer Wine differential exports, two unavailable-driver open exports");
}
