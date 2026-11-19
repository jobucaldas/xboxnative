// SPDX-License-Identifier: GPL-3.0-or-later
// PE32 waveOut guest fixture and x86 Play host runner.
#ifdef X86_WAVEOUT_GUEST
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

static volatile LONG callback_count;
static volatile LONG callback_order;
static volatile LONG open_before_return;
static volatile LONG open_returned;
static volatile LONG close_before_return;
static volatile LONG close_returned;
static volatile LONG callback_after_close;
static volatile LONG window_callback_count;
static HANDLE function_done_event;
static HWAVEOUT window_output;
static WAVEHDR* callback_header;
static WAVEHDR* reset_headers[3];
static volatile LONG reset_done_count;
static volatile LONG reset_order[3];
static volatile LONG close_on_done;
static volatile LONG close_done_closed;
static volatile MMRESULT close_done_result = MMSYSERR_ERROR;
static volatile LONG drain_done;
static WAVEHDR* window_header;
static LRESULT CALLBACK wave_window(HWND, UINT message, WPARAM output, LPARAM header) {
    if (message == WOM_DONE && output == reinterpret_cast<WPARAM>(window_output) &&
        header == reinterpret_cast<LPARAM>(window_header) && (window_header->dwFlags & WHDR_DONE))
        InterlockedIncrement(&window_callback_count);
    return 0;
}
static void CALLBACK drain_wave_done(HWAVEOUT, UINT message, DWORD_PTR, DWORD_PTR, DWORD_PTR) {
    if (message == WOM_DONE) InterlockedIncrement(&drain_done);
}
static void CALLBACK wave_done(HWAVEOUT handle, UINT message, DWORD_PTR instance, DWORD_PTR header, DWORD_PTR) {
    if (instance != 0x71a0u) return;
    if (InterlockedCompareExchange(&close_returned, 0, 0)) InterlockedIncrement(&callback_after_close);
    if (message == WOM_OPEN && handle && !InterlockedCompareExchange(&open_returned, 0, 0))
        InterlockedExchange(&open_before_return, 1);
    if (message == WOM_CLOSE && !InterlockedCompareExchange(&close_returned, 0, 0))
        InterlockedExchange(&close_before_return, 1);
    if (message == WOM_CLOSE && InterlockedCompareExchange(&close_on_done, 0, 0))
        InterlockedExchange(&close_done_closed, 1);
    if (message != WOM_DONE) return;
    if (InterlockedCompareExchange(&close_on_done, 0, 0)) {
        close_done_result = waveOutClose(handle); return;
    }
    for (unsigned i = 0; i < 3; ++i) if (header == reinterpret_cast<DWORD_PTR>(reset_headers[i]) &&
        (reset_headers[i]->dwFlags & WHDR_DONE) && !(reset_headers[i]->dwFlags & WHDR_INQUEUE)) {
        LONG order = InterlockedIncrement(&reset_done_count) - 1;
        if (order < 3) reset_order[order] = LONG(i);
    }
    if (header != reinterpret_cast<DWORD_PTR>(callback_header)) return;
    if ((callback_header->dwFlags & WHDR_DONE) && !(callback_header->dwFlags & WHDR_INQUEUE))
        InterlockedExchange(&callback_order, 1);
    InterlockedIncrement(&callback_count);
    SetEvent(function_done_event);
}
int main() {
    WAVEOUTCAPSA caps_a{}; WAVEOUTCAPSW caps_w{}; UINT devices = waveOutGetNumDevs();
    WAVEFORMATEX format{WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
    if (devices == 0) {
        HWAVEOUT no_output = nullptr; WAVEHDR no_header{}; MMTIME no_time{}; DWORD no_volume = 0;
        if (waveOutGetDevCapsA(WAVE_MAPPER, &caps_a, sizeof(caps_a)) != MMSYSERR_NODRIVER ||
            waveOutGetDevCapsW(WAVE_MAPPER, &caps_w, sizeof(caps_w)) != MMSYSERR_NODRIVER ||
            waveOutOpen(nullptr, WAVE_MAPPER, &format, 0, 0, WAVE_FORMAT_QUERY) != MMSYSERR_NOERROR ||
            waveOutOpen(&no_output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NODRIVER || no_output ||
            waveOutPrepareHeader(nullptr, &no_header, sizeof(no_header)) != MMSYSERR_INVALHANDLE ||
            waveOutUnprepareHeader(nullptr, &no_header, sizeof(no_header)) != MMSYSERR_INVALHANDLE ||
            waveOutWrite(nullptr, &no_header, sizeof(no_header)) != MMSYSERR_INVALHANDLE ||
            waveOutGetPosition(nullptr, &no_time, sizeof(no_time)) != MMSYSERR_INVALHANDLE ||
            waveOutReset(nullptr) != MMSYSERR_INVALHANDLE || waveOutPause(nullptr) != MMSYSERR_INVALHANDLE ||
            waveOutRestart(nullptr) != MMSYSERR_INVALHANDLE || waveOutSetVolume(nullptr, 0) != MMSYSERR_INVALHANDLE ||
            waveOutGetVolume(nullptr, &no_volume) != MMSYSERR_INVALHANDLE || waveOutClose(nullptr) != MMSYSERR_INVALHANDLE ||
            waveOutMessage(nullptr, 0, 0, 0) != MMSYSERR_NODRIVER ||
            waveOutGetErrorTextA(0, nullptr, 0) != MMSYSERR_NODRIVER || waveOutGetErrorTextW(0, nullptr, 0) != MMSYSERR_NODRIVER) return 48;
        WAVEFORMATEX invalid_format = format; invalid_format.wFormatTag = 3;
        if (waveOutOpen(nullptr, WAVE_MAPPER, &invalid_format, 0, 0, WAVE_FORMAT_QUERY) != WAVERR_BADFORMAT) return 49;
        puts("X86-WAVEOUT PASS: no-device format query and PE32 NODRIVER/INVALHANDLE contract"); return 0;
    }
    if (devices != 1 || waveOutGetDevCapsA(WAVE_MAPPER, &caps_a, sizeof(caps_a)) != MMSYSERR_NOERROR ||
        waveOutGetDevCapsW(WAVE_MAPPER, &caps_w, sizeof(caps_w)) != MMSYSERR_NOERROR) return 9;
    if (waveOutOpen(nullptr, 0xffffffffu, &format, 0, 0, WAVE_FORMAT_QUERY) != MMSYSERR_NOERROR) return 27;
    WAVEFORMATEX invalid_format = format; invalid_format.wFormatTag = 3;
    if (waveOutOpen(nullptr, 0xffffffffu, &invalid_format, 0, 0, WAVE_FORMAT_QUERY) != WAVERR_BADFORMAT) return 28;
    HWAVEIN input = nullptr;
    if (waveInGetNumDevs() != 0 || waveInOpen(&input, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NODRIVER || input) return 8;
    function_done_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!function_done_event) return 33;
    HWAVEOUT output = nullptr;
    MMRESULT result = waveOutOpen(&output, WAVE_MAPPER, &format,
        reinterpret_cast<DWORD_PTR>(&wave_done), 0x71a0u, CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR || !output) { printf("X86-WAVEOUT FAIL: waveOutOpen=%u\n", result); return 10; }
    InterlockedExchange(&open_returned, 1);
    if (!open_before_return) return 30;
    static char samples[192000];
    WAVEHDR header{};
    header.lpData = samples;
    header.dwBufferLength = sizeof(samples);
    callback_header = &header;
    result = waveOutPrepareHeader(output, &header, sizeof(header));
    if (result != MMSYSERR_NOERROR || !(header.dwFlags & WHDR_PREPARED)) return 11;
    result = waveOutWrite(output, &header, sizeof(header));
    if (result != MMSYSERR_NOERROR || !(header.dwFlags & WHDR_INQUEUE)) return 12;
    MMRESULT volume_result = waveOutSetVolume(output, 0x12345678u);
    if (volume_result != MMSYSERR_NOERROR) { printf("X86-WAVEOUT volume set failed: %u\n", volume_result); return 40; }
    if (waveOutMessage(output, 0, 0, 0) != MMSYSERR_NODRIVER ||
        waveOutGetErrorTextA(0, nullptr, 0) != MMSYSERR_NODRIVER ||
        waveOutGetErrorTextW(0, nullptr, 0) != MMSYSERR_NODRIVER) return 45;
    DWORD volume = 0;
    if (waveOutGetVolume(output, &volume) != MMSYSERR_NOERROR || volume != 0x12345678u) return 41;
    if (waveOutClose(output) != WAVERR_STILLPLAYING) return 13;
    if (waveOutPause(output) != MMSYSERR_NOERROR) return 42;
    MMTIME paused_before{}; paused_before.wType = TIME_BYTES;
    if (waveOutGetPosition(output, &paused_before, sizeof(paused_before)) != MMSYSERR_NOERROR) return 14;
    Sleep(40);
    MMTIME paused_after{}; paused_after.wType = TIME_BYTES;
    if (waveOutGetPosition(output, &paused_after, sizeof(paused_after)) != MMSYSERR_NOERROR || paused_after.u.cb != paused_before.u.cb) return 43;
    if (waveOutRestart(output) != MMSYSERR_NOERROR) return 44;
    Sleep(40);
    MMTIME after{}; after.wType = TIME_BYTES;
    if (waveOutGetPosition(output, &after, sizeof(after)) != MMSYSERR_NOERROR || after.u.cb <= paused_after.u.cb) return 15;
    if (waveOutReset(output) != MMSYSERR_NOERROR || WaitForSingleObject(function_done_event, 5000) != WAIT_OBJECT_0) return 16;
    MMTIME reset_position{}; reset_position.wType = TIME_SAMPLES;
    if (waveOutGetPosition(output, &reset_position, sizeof(reset_position)) != MMSYSERR_NOERROR || reset_position.u.sample != 0) return 29;
    if (callback_count != 1 || callback_order != 1 || !(header.dwFlags & WHDR_DONE) || (header.dwFlags & WHDR_INQUEUE)) return 17;
    if (waveOutUnprepareHeader(output, &header, sizeof(header)) != MMSYSERR_NOERROR) return 18;
    if (waveOutReset(output) != MMSYSERR_NOERROR || waveOutClose(output) != MMSYSERR_NOERROR) return 19;
    InterlockedExchange(&close_returned, 1);
    if (!close_before_return) return 31;
    Sleep(100);
    if (callback_after_close) return 32;
    HWAVEOUT callback_close_output = nullptr; WAVEHDR callback_close_header{}; char callback_close_samples[1920]{};
    callback_close_header.lpData = callback_close_samples; callback_close_header.dwBufferLength = sizeof(callback_close_samples);
    if (waveOutOpen(&callback_close_output, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(&wave_done), 0x71a0u, CALLBACK_FUNCTION) != MMSYSERR_NOERROR ||
        waveOutPrepareHeader(callback_close_output, &callback_close_header, sizeof(callback_close_header)) != MMSYSERR_NOERROR) return 46;
    InterlockedExchange(&close_on_done, 1); close_done_closed = 0; close_done_result = MMSYSERR_ERROR;
    if (waveOutWrite(callback_close_output, &callback_close_header, sizeof(callback_close_header)) != MMSYSERR_NOERROR ||
        waveOutReset(callback_close_output) != MMSYSERR_NOERROR || close_done_result != WAVERR_STILLPLAYING || !close_done_closed) return 47;
    InterlockedExchange(&close_on_done, 0);
    HWAVEOUT drain_output = nullptr; WAVEHDR drain_header{}; char drain_samples[1920]{};
    drain_header.lpData = drain_samples; drain_header.dwBufferLength = sizeof(drain_samples); drain_done = 0;
    if (waveOutOpen(&drain_output, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(&drain_wave_done), 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR ||
        waveOutPrepareHeader(drain_output, &drain_header, sizeof(drain_header)) != MMSYSERR_NOERROR ||
        waveOutWrite(drain_output, &drain_header, sizeof(drain_header)) != MMSYSERR_NOERROR) return 50;
    Sleep(1000);
    if (waveOutClose(drain_output) != MMSYSERR_NOERROR || drain_done != 1) return 51;
    HWAVEOUT reset_output = nullptr;
    if (waveOutOpen(&reset_output, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(&wave_done), 0x71a0u, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) return 34;
    WAVEHDR reset_buffer[3]{};
    for (unsigned i = 0; i < 3; ++i) {
        reset_buffer[i].lpData = samples; reset_buffer[i].dwBufferLength = sizeof(samples); reset_headers[i] = &reset_buffer[i];
        if (waveOutPrepareHeader(reset_output, &reset_buffer[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR ||
            waveOutWrite(reset_output, &reset_buffer[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return 35;
    }
    if (waveOutReset(reset_output) != MMSYSERR_NOERROR || reset_done_count != 3 ||
        reset_order[0] != 0 || reset_order[1] != 1 || reset_order[2] != 2) return 36;
    for (unsigned i = 0; i < 3; ++i) if (!(reset_buffer[i].dwFlags & WHDR_DONE) || (reset_buffer[i].dwFlags & WHDR_INQUEUE)) return 37;
    for (unsigned i = 0; i < 3; ++i) if (waveOutUnprepareHeader(reset_output, &reset_buffer[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return 38;
    if (waveOutClose(reset_output) != MMSYSERR_NOERROR) return 39;
    CloseHandle(function_done_event);
    HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!event) return 20;
    output = nullptr;
    if (waveOutOpen(&output, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(event), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR ||
        WaitForSingleObject(event, 0) != WAIT_OBJECT_0 || !ResetEvent(event)) return 21;
    WAVEHDR event_header{}; event_header.lpData = samples; event_header.dwBufferLength = sizeof(samples); MMTIME event_time{}; event_time.wType = TIME_BYTES;
    if (waveOutPrepareHeader(output, &event_header, sizeof(event_header)) != MMSYSERR_NOERROR ||
        waveOutWrite(output, &event_header, sizeof(event_header)) != MMSYSERR_NOERROR) return 22;
    Sleep(1000);
    if (waveOutGetPosition(output, &event_time, sizeof(event_time)) != MMSYSERR_NOERROR ||
        WaitForSingleObject(event, 5000) != WAIT_OBJECT_0 || !(event_header.dwFlags & WHDR_DONE) ||
        waveOutReset(output) != MMSYSERR_NOERROR || !(event_header.dwFlags & WHDR_DONE) ||
        waveOutUnprepareHeader(output, &event_header, sizeof(event_header)) != MMSYSERR_NOERROR) return 22;
    char followup_samples[48000]{}; WAVEHDR followup_header{}; followup_header.lpData = followup_samples; followup_header.dwBufferLength = sizeof(followup_samples);
    if (waveOutPrepareHeader(output, &followup_header, sizeof(followup_header)) != MMSYSERR_NOERROR ||
        waveOutWrite(output, &followup_header, sizeof(followup_header)) != MMSYSERR_NOERROR ||
        WaitForSingleObject(event, 0) != WAIT_TIMEOUT || WaitForSingleObject(event, 5000) != WAIT_OBJECT_0 ||
        !(followup_header.dwFlags & WHDR_DONE) || waveOutReset(output) != MMSYSERR_NOERROR ||
        waveOutUnprepareHeader(output, &followup_header, sizeof(followup_header)) != MMSYSERR_NOERROR ||
        waveOutClose(output) != MMSYSERR_NOERROR) return 22;
    WNDCLASSEXA window_class{}; window_class.cbSize = sizeof(window_class); window_class.lpfnWndProc = wave_window;
    window_class.lpszClassName = "WaveOutFixtureWindow";
    if (!RegisterClassExA(&window_class)) return 23;
    HWND window = CreateWindowExA(0, window_class.lpszClassName, "", 0, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
    if (!window) return 24;
    output = nullptr;
    if (waveOutOpen(&output, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(window), 0, CALLBACK_WINDOW) != MMSYSERR_NOERROR) return 25;
    WAVEHDR window_buffer{}; window_buffer.lpData = samples; window_buffer.dwBufferLength = sizeof(samples);
    window_output = output; window_header = &window_buffer;
    if (waveOutPrepareHeader(output, &window_buffer, sizeof(window_buffer)) != MMSYSERR_NOERROR ||
        waveOutWrite(output, &window_buffer, sizeof(window_buffer)) != MMSYSERR_NOERROR ||
        waveOutReset(output) != MMSYSERR_NOERROR || window_callback_count != 1 ||
        waveOutUnprepareHeader(output, &window_buffer, sizeof(window_buffer)) != MMSYSERR_NOERROR ||
        waveOutClose(output) != MMSYSERR_NOERROR) return 26;
    puts("X86-WAVEOUT PASS: function, event, and window callbacks; query validation, reset position, WHDR_DONE ordering and close semantics");
    return 0;
}
#else
#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include "../host/x86/x86_play.h"

static uintptr_t shim_export(HMODULE module, const char *name) {
    auto base = reinterpret_cast<uint8_t *>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    const auto &directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!directory.VirtualAddress || !directory.Size) return 0;
    auto exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY *>(base + directory.VirtualAddress);
    auto names = reinterpret_cast<DWORD *>(base + exports->AddressOfNames);
    auto ordinals = reinterpret_cast<WORD *>(base + exports->AddressOfNameOrdinals);
    auto functions = reinterpret_cast<DWORD *>(base + exports->AddressOfFunctions);
    for (DWORD i = 0; i < exports->NumberOfNames; ++i)
        if (!strcmp(reinterpret_cast<const char *>(base + names[i]), name))
            return reinterpret_cast<uintptr_t>(base + functions[ordinals[i]]);
    return 0;
}

using WaveOutClose = MMRESULT (WINAPI *)(HWAVEOUT);
static HANDLE waveout_done_event;
static WaveOutClose waveout_close;
static volatile MMRESULT waveout_callback_close_result = MMSYSERR_ERROR;
struct StressCallbackState { volatile LONG closed, late; };
struct HandleStress {
    HWAVEOUT output;
    MMRESULT (WINAPI *position)(HWAVEOUT, LPMMTIME, UINT);
    MMRESULT (WINAPI *write)(HWAVEOUT, LPWAVEHDR, UINT);
    volatile LONG *stop, *failed, *progress;
    WAVEHDR header;
    char samples[1920];
};
static DWORD WINAPI handle_stress_thread(void *arg) {
    auto *stress = static_cast<HandleStress *>(arg);
    while (!InterlockedCompareExchange(stress->stop, 0, 0)) {
        MMTIME time{}; time.wType = TIME_BYTES;
        MMRESULT positioned = stress->position(stress->output, &time, sizeof(time));
        if (positioned == MMSYSERR_INVALHANDLE) break;
        if (positioned != MMSYSERR_NOERROR) { InterlockedExchange(stress->failed, 1); break; }
        if (stress->write) {
            MMRESULT result = stress->write(stress->output, &stress->header, sizeof(stress->header));
            if (result == MMSYSERR_INVALHANDLE) break;
            if (result != MMSYSERR_NOERROR && result != WAVERR_STILLPLAYING) {
                InterlockedExchange(stress->failed, 1); break;
            }
        }
        InterlockedIncrement(stress->progress);
        Sleep(1);
    }
    return 0;
}
static void CALLBACK stress_waveout_done(HWAVEOUT, UINT message, DWORD_PTR instance, DWORD_PTR, DWORD_PTR) {
    if (message == WOM_DONE) {
        auto *state = reinterpret_cast<StressCallbackState *>(instance);
        if (InterlockedCompareExchange(&state->closed, 0, 0)) InterlockedIncrement(&state->late);
    }
}
static void CALLBACK close_from_waveout_done(HWAVEOUT handle, UINT message, DWORD_PTR, DWORD_PTR, DWORD_PTR) {
    if (message == WOM_DONE) {
        waveout_callback_close_result = waveout_close(handle);
        SetEvent(waveout_done_event);
    }
}

int main(int argc, char **argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: x86-waveout-integration <dsound.dll> <guest.exe>\n"); return 2; }
    HMODULE shim = LoadLibraryA(argv[1]);
    if (!shim) { std::fprintf(stderr, "FAIL: LoadLibrary dsound shim: %lu\n", GetLastError()); return 2; }
    auto count = reinterpret_cast<UINT (WINAPI *)()>(shim_export(shim, "XbnWaveOutGetNumDevs"));
    auto caps_a = reinterpret_cast<MMRESULT (WINAPI *)(UINT_PTR, LPWAVEOUTCAPSA, UINT)>(shim_export(shim, "XbnWaveOutGetDevCapsA"));
    auto caps_w = reinterpret_cast<MMRESULT (WINAPI *)(UINT_PTR, LPWAVEOUTCAPSW, UINT)>(shim_export(shim, "XbnWaveOutGetDevCapsW"));
    auto open = reinterpret_cast<MMRESULT (WINAPI *)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD)>(shim_export(shim, "XbnWaveOutOpen"));
    waveout_close = reinterpret_cast<WaveOutClose>(shim_export(shim, "XbnWaveOutClose"));
    auto prepare = reinterpret_cast<MMRESULT (WINAPI *)(HWAVEOUT, LPWAVEHDR, UINT)>(shim_export(shim, "XbnWaveOutPrepareHeader"));
    auto unprepare = reinterpret_cast<MMRESULT (WINAPI *)(HWAVEOUT, LPWAVEHDR, UINT)>(shim_export(shim, "XbnWaveOutUnprepareHeader"));
    auto write = reinterpret_cast<MMRESULT (WINAPI *)(HWAVEOUT, LPWAVEHDR, UINT)>(shim_export(shim, "XbnWaveOutWrite"));
    auto position = reinterpret_cast<MMRESULT (WINAPI *)(HWAVEOUT, LPMMTIME, UINT)>(shim_export(shim, "XbnWaveOutGetPosition"));
    auto reset = reinterpret_cast<MMRESULT (WINAPI *)(HWAVEOUT)>(shim_export(shim, "XbnWaveOutReset"));
    auto pause = reinterpret_cast<MMRESULT (WINAPI *)(HWAVEOUT)>(shim_export(shim, "XbnWaveOutPause"));
    auto restart = reinterpret_cast<MMRESULT (WINAPI *)(HWAVEOUT)>(shim_export(shim, "XbnWaveOutRestart"));
    auto set_volume = reinterpret_cast<MMRESULT (WINAPI *)(HWAVEOUT, DWORD)>(shim_export(shim, "XbnWaveOutSetVolume"));
    auto get_volume = reinterpret_cast<MMRESULT (WINAPI *)(HWAVEOUT, LPDWORD)>(shim_export(shim, "XbnWaveOutGetVolume"));
    if (!count || !caps_a || !caps_w || !open || !waveout_close || !prepare || !unprepare || !write || !position || !reset ||
        !pause || !restart || !set_volume || !get_volume) { std::fprintf(stderr, "FAIL: internal waveOut exports missing\n"); return 2; }
    WAVEFORMATEX format{WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
    if (open(nullptr, 0xffffffffu, &format, 0, 0, WAVE_FORMAT_QUERY) != MMSYSERR_NOERROR) {
        std::fprintf(stderr, "FAIL: x64 WAVE_FORMAT_QUERY rejected a null handle or device id\n"); return 2;
    }
    format.wFormatTag = 3;
    if (open(nullptr, 0xffffffffu, &format, 0, 0, WAVE_FORMAT_QUERY) != WAVERR_BADFORMAT) {
        std::fprintf(stderr, "FAIL: x64 WAVE_FORMAT_QUERY accepted an invalid format\n"); return 2;
    }
    format.wFormatTag = WAVE_FORMAT_PCM;
    HWAVEOUT live = nullptr;
    MMRESULT live_result = open(&live, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (live_result != MMSYSERR_NOERROR && live_result != MMSYSERR_NODRIVER) {
        std::fprintf(stderr, "FAIL: audio open=%u\n", live_result); return 2;
    }
    if (live_result == MMSYSERR_NODRIVER) {
        WAVEOUTCAPSA no_device_caps_a{}; WAVEOUTCAPSW no_device_caps_w{}; MMTIME no_device_time{}; DWORD no_device_volume = 0;
        WAVEHDR no_device_header{};
        if (count() != 0 || live || caps_a(WAVE_MAPPER, &no_device_caps_a, sizeof(no_device_caps_a)) != MMSYSERR_NODRIVER ||
            caps_w(WAVE_MAPPER, &no_device_caps_w, sizeof(no_device_caps_w)) != MMSYSERR_NODRIVER ||
            prepare(nullptr, &no_device_header, sizeof(no_device_header)) != MMSYSERR_INVALHANDLE ||
            unprepare(nullptr, &no_device_header, sizeof(no_device_header)) != MMSYSERR_INVALHANDLE ||
            write(nullptr, &no_device_header, sizeof(no_device_header)) != MMSYSERR_INVALHANDLE ||
            position(nullptr, &no_device_time, sizeof(no_device_time)) != MMSYSERR_INVALHANDLE ||
            reset(nullptr) != MMSYSERR_INVALHANDLE || pause(nullptr) != MMSYSERR_INVALHANDLE ||
            restart(nullptr) != MMSYSERR_INVALHANDLE || set_volume(nullptr, 0) != MMSYSERR_INVALHANDLE ||
            get_volume(nullptr, &no_device_volume) != MMSYSERR_INVALHANDLE || waveout_close(nullptr) != MMSYSERR_INVALHANDLE) {
            std::fprintf(stderr, "FAIL: no-device waveOut contract (open=%u)\n", live_result); return 2;
        }
        std::puts("X64-WAVEOUT PASS: format query and no-device NODRIVER/INVALHANDLE contract");
        std::puts("SKIP device playback: no audio device");
    }
    if (live_result == MMSYSERR_NOERROR) {
    HWAVEOUT mono = nullptr;
    WAVEFORMATEX mono_format{WAVE_FORMAT_PCM, 1, 48000, 96000, 2, 16, 0};
    if (open(&mono, WAVE_MAPPER, &mono_format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR ||
        set_volume(mono, 0x12345678u) != MMSYSERR_NOERROR || waveout_close(mono) != MMSYSERR_NOERROR) {
        std::fprintf(stderr, "FAIL: mono volume\n"); return 2;
    }
    char paused_samples[19200]{};
    WAVEHDR paused_header[2]{};
    for (auto &header : paused_header) {
        header.lpData = paused_samples; header.dwBufferLength = sizeof(paused_samples);
        if (prepare(live, &header, sizeof(header)) != MMSYSERR_NOERROR) return 2;
    }
    if (pause(live) != MMSYSERR_NOERROR) return 2;
    for (unsigned i = 0; i < 2; ++i) {
        if (write(live, &paused_header[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return 2;
        Sleep(200);
        if (!(paused_header[i].dwFlags & WHDR_INQUEUE) || (paused_header[i].dwFlags & WHDR_DONE)) {
            std::fprintf(stderr, "FAIL: paused write played (pass %u)\n", i); return 2;
        }
        if (i == 0 && reset(live) != MMSYSERR_NOERROR) return 2;
    }
    if (restart(live) != MMSYSERR_NOERROR) return 2;
    for (unsigned i = 0; i < 500 && !(paused_header[1].dwFlags & WHDR_DONE); ++i) Sleep(1);
    if (!(paused_header[1].dwFlags & WHDR_DONE)) { std::fprintf(stderr, "FAIL: restart did not play queued buffer\n"); return 2; }
    std::puts("X64-WAVEOUT PASS: mono volume and pause across write/reset/restart");
    volatile LONG stop = 0, failed = 0, progress = 0;
    HandleStress workers[2]{};
    HANDLE threads[2]{};
    for (unsigned i = 0; i < 2; ++i) {
        workers[i].output = live; workers[i].position = position; workers[i].write = write;
        workers[i].stop = &stop; workers[i].failed = &failed; workers[i].progress = &progress;
        workers[i].header.lpData = workers[i].samples; workers[i].header.dwBufferLength = sizeof(workers[i].samples);
        if (prepare(live, &workers[i].header, sizeof(workers[i].header)) != MMSYSERR_NOERROR) return 2;
        threads[i] = CreateThread(nullptr, 0, handle_stress_thread, &workers[i], 0, nullptr);
        if (!threads[i]) return 2;
    }
    for (unsigned i = 0; i < 200; ++i) {
        HWAVEOUT other = nullptr;
        if (open(&other, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR ||
            waveout_close(other) != MMSYSERR_NOERROR) { InterlockedExchange(&failed, 1); break; }
    }
    if (!progress) InterlockedExchange(&failed, 1);
    InterlockedExchange(&stop, 1);
    if (WaitForMultipleObjects(2, threads, TRUE, 15000) != WAIT_OBJECT_0) return 2;
    for (auto thread : threads) CloseHandle(thread);
    if (failed || reset(live) != MMSYSERR_NOERROR || waveout_close(live) != MMSYSERR_NOERROR) {
        std::fprintf(stderr, "FAIL: concurrent live handle lookups and open/close\n"); return 2;
    }
    std::puts("X64-WAVEOUT PASS: concurrent live handle lookups and 200 open/close operations");
    HWAVEOUT reset_race = nullptr;
    if (open(&reset_race, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return 2;
    volatile LONG reset_stop = 0, reset_failed = 0, reset_progress = 0;
    HandleStress reset_worker{}; reset_worker.output = reset_race; reset_worker.position = position; reset_worker.write = write;
    reset_worker.stop = &reset_stop; reset_worker.failed = &reset_failed; reset_worker.progress = &reset_progress;
    reset_worker.header.lpData = reset_worker.samples; reset_worker.header.dwBufferLength = sizeof(reset_worker.samples);
    if (prepare(reset_race, &reset_worker.header, sizeof(reset_worker.header)) != MMSYSERR_NOERROR) return 2;
    HANDLE reset_thread = CreateThread(nullptr, 0, handle_stress_thread, &reset_worker, 0, nullptr);
    if (!reset_thread) return 2;
    for (unsigned wait = 0; wait < 1000 && !reset_progress; ++wait) Sleep(1);
    if (!reset_progress) return 2;
    for (unsigned i = 0; i < 50; ++i) if (reset(reset_race) != MMSYSERR_NOERROR) return 2;
    InterlockedExchange(&reset_stop, 1);
    if (WaitForSingleObject(reset_thread, 5000) != WAIT_OBJECT_0 || reset_failed ||
        reset(reset_race) != MMSYSERR_NOERROR || waveout_close(reset_race) != MMSYSERR_NOERROR) return 2;
    CloseHandle(reset_thread);
    HWAVEOUT same_handle = nullptr;
    if (open(&same_handle, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return 2;
    volatile LONG same_stop = 0, same_failed = 0, same_progress = 0;
    HandleStress same_worker{}; same_worker.output = same_handle; same_worker.position = position;
    same_worker.stop = &same_stop; same_worker.failed = &same_failed; same_worker.progress = &same_progress;
    HANDLE same_thread = CreateThread(nullptr, 0, handle_stress_thread, &same_worker, 0, nullptr);
    if (!same_thread) return 2;
    for (unsigned i = 0; i < 1000 && !same_progress; ++i) Sleep(1);
    if (!same_progress || waveout_close(same_handle) != MMSYSERR_NOERROR ||
        WaitForSingleObject(same_thread, 5000) != WAIT_OBJECT_0 || same_failed) return 2;
    CloseHandle(same_thread);
    for (unsigned i = 0; i < 100; ++i) {
        HWAVEOUT racing = nullptr;
        WAVEHDR racing_header{}; char racing_samples[1920]{};
        if (open(&racing, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return 2;
        racing_header.lpData = racing_samples; racing_header.dwBufferLength = sizeof(racing_samples);
        if (prepare(racing, &racing_header, sizeof(racing_header)) != MMSYSERR_NOERROR) return 2;
        volatile LONG racing_stop = 0, racing_failed = 0, racing_progress = 0;
        HandleStress writer{}; writer.output = racing; writer.position = position; writer.write = write;
        writer.stop = &racing_stop; writer.failed = &racing_failed; writer.progress = &racing_progress; writer.header = racing_header;
        memcpy(writer.samples, racing_samples, sizeof(writer.samples)); writer.header.lpData = writer.samples;
        HANDLE writer_thread = CreateThread(nullptr, 0, handle_stress_thread, &writer, 0, nullptr);
        if (!writer_thread) return 2;
        for (unsigned wait = 0; wait < 1000 && !racing_progress; ++wait) Sleep(1);
        if (!racing_progress) return 2;
        MMRESULT close_result = waveout_close(racing);
        if (close_result == WAVERR_STILLPLAYING) {
            InterlockedExchange(&racing_stop, 1);
            if (WaitForSingleObject(writer_thread, 5000) != WAIT_OBJECT_0 || reset(racing) != MMSYSERR_NOERROR ||
                waveout_close(racing) != MMSYSERR_NOERROR) return 2;
        } else if (close_result != MMSYSERR_NOERROR) return 2;
        InterlockedExchange(&racing_stop, 1);
        if (WaitForSingleObject(writer_thread, 5000) != WAIT_OBJECT_0 || racing_failed) return 2;
        CloseHandle(writer_thread);
    }
    waveout_done_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!waveout_done_event) { std::fprintf(stderr, "FAIL: create callback-close event\n"); return 2; }
    HWAVEOUT callback_output = nullptr;
    WAVEHDR callback_header{};
    char callback_samples[48000]{};
    callback_header.lpData = callback_samples;
    callback_header.dwBufferLength = sizeof(callback_samples);
    if (open(&callback_output, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(&close_from_waveout_done), 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR ||
        prepare(callback_output, &callback_header, sizeof(callback_header)) != MMSYSERR_NOERROR ||
        write(callback_output, &callback_header, sizeof(callback_header)) != MMSYSERR_NOERROR ||
        WaitForSingleObject(waveout_done_event, 15000) != WAIT_OBJECT_0 || waveout_callback_close_result != MMSYSERR_NOERROR) {
        std::fprintf(stderr, "FAIL: close from WOM_DONE callback (result=%u)\n", waveout_callback_close_result); return 2;
    }
    CloseHandle(waveout_done_event);
    constexpr unsigned stress_iterations = 100;
    StressCallbackState stress[stress_iterations]{};
    for (unsigned i = 0; i < stress_iterations; ++i) {
        HWAVEOUT output = nullptr;
        WAVEHDR header{};
        char samples[1920]{};
        header.lpData = samples; header.dwBufferLength = sizeof(samples);
        if (open(&output, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(&stress_waveout_done), reinterpret_cast<DWORD_PTR>(&stress[i]), CALLBACK_FUNCTION) != MMSYSERR_NOERROR ||
            prepare(output, &header, sizeof(header)) != MMSYSERR_NOERROR ||
            write(output, &header, sizeof(header)) != MMSYSERR_NOERROR) return 2;
        MMRESULT result = WAVERR_STILLPLAYING;
        for (unsigned attempt = 0; attempt < 15000 && result == WAVERR_STILLPLAYING; ++attempt) {
            result = waveout_close(output);
            if (result == WAVERR_STILLPLAYING) Sleep(1);
        }
        if (result != MMSYSERR_NOERROR) { std::fprintf(stderr, "FAIL: stress close=%u iteration=%u\n", result, i); return 2; }
        InterlockedExchange(&stress[i].closed, 1);
    }
    Sleep(100);
    for (unsigned i = 0; i < stress_iterations; ++i) if (stress[i].late) {
        std::fprintf(stderr, "FAIL: callback after close iteration=%u\n", i); return 2;
    }
    std::puts("X64-WAVEOUT PASS: format query, callback close, and 100 close/completion races");
    }
    std::ifstream f(argv[2], std::ios::binary);
    if (!f) { std::fprintf(stderr, "FAIL: cannot open guest fixture\n"); return 2; }
    std::vector<uint8_t> image{std::istreambuf_iterator<char>(f), {}};
    std::string output;
    auto result = x86::run_image(image, "waveout-fixture.exe", "waveout-fixture.exe",
        [&output](const std::string &s) { output += s; }, nullptr, 500ull * 1000 * 1000,
        nullptr, false, nullptr, nullptr, [shim](x86::Host &host) {
            host.dsound_export = [shim](const char *name) -> uintptr_t {
                return name ? shim_export(shim, name) : 0;
            };
            host.register_window_class = [](const std::u16string &) { return true; };
            host.create_window = [](const x86::Host::WindowCreate &) { return uintptr_t(0x1234); };
        });
    if (!result.fault.empty()) std::fprintf(stderr, "X86-WAVEOUT FAIL: %s: %s\n", result.fault_kind.c_str(), result.fault.c_str());
    if (!output.empty()) std::fwrite(output.data(), 1, output.size(), stdout);
    bool passed = result.loaded && result.finished && result.exit_code == 0 &&
                  output.find("X86-WAVEOUT PASS:") != std::string::npos;
    std::printf("X86-WAVEOUT %s: interpreter loaded=%d finished=%d exit=%u imports=%u bound=%u\n",
        passed ? "PASS" : "FAIL", result.loaded, result.finished, result.exit_code, result.imports, result.bound);
    FreeLibrary(shim);
    return passed ? 0 : 1;
}
#endif
