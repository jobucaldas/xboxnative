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
static volatile LONG window_callback_count;
static HWAVEOUT window_output;
static WAVEHDR* callback_header;
static WAVEHDR* window_header;
static LRESULT CALLBACK wave_window(HWND, UINT message, WPARAM output, LPARAM header) {
    if (message == WOM_DONE && output == reinterpret_cast<WPARAM>(window_output) &&
        header == reinterpret_cast<LPARAM>(window_header) && (window_header->dwFlags & WHDR_DONE))
        InterlockedIncrement(&window_callback_count);
    return 0;
}
static void CALLBACK wave_done(HWAVEOUT, UINT message, DWORD_PTR instance, DWORD_PTR header, DWORD_PTR) {
    if (message != WOM_DONE || instance != 0x71a0u || header != reinterpret_cast<DWORD_PTR>(callback_header)) return;
    if ((callback_header->dwFlags & WHDR_DONE) && !(callback_header->dwFlags & WHDR_INQUEUE))
        InterlockedExchange(&callback_order, 1);
    InterlockedIncrement(&callback_count);
}
int main() {
    WAVEOUTCAPSA caps_a{}; WAVEOUTCAPSW caps_w{};
    if (waveOutGetNumDevs() != 1 || waveOutGetDevCapsA(WAVE_MAPPER, &caps_a, sizeof(caps_a)) != MMSYSERR_NOERROR ||
        waveOutGetDevCapsW(WAVE_MAPPER, &caps_w, sizeof(caps_w)) != MMSYSERR_NOERROR) return 9;
    WAVEFORMATEX format{WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
    HWAVEIN input = nullptr;
    if (waveInGetNumDevs() != 0 || waveInOpen(&input, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NODRIVER || input) return 8;
    HWAVEOUT output = nullptr;
    MMRESULT result = waveOutOpen(&output, WAVE_MAPPER, &format,
        reinterpret_cast<DWORD_PTR>(&wave_done), 0x71a0u, CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR || !output) { printf("X86-WAVEOUT FAIL: waveOutOpen=%u\n", result); return 10; }
    static char samples[192000];
    WAVEHDR header{};
    header.lpData = samples;
    header.dwBufferLength = sizeof(samples);
    callback_header = &header;
    result = waveOutPrepareHeader(output, &header, sizeof(header));
    if (result != MMSYSERR_NOERROR || !(header.dwFlags & WHDR_PREPARED)) return 11;
    result = waveOutWrite(output, &header, sizeof(header));
    if (result != MMSYSERR_NOERROR || !(header.dwFlags & WHDR_INQUEUE)) return 12;
    if (waveOutClose(output) != WAVERR_STILLPLAYING) return 13;
    MMTIME before{}; before.wType = TIME_BYTES;
    if (waveOutGetPosition(output, &before, sizeof(before)) != MMSYSERR_NOERROR) return 14;
    Sleep(40);
    MMTIME after{}; after.wType = TIME_BYTES;
    if (waveOutGetPosition(output, &after, sizeof(after)) != MMSYSERR_NOERROR || after.u.cb <= before.u.cb) return 15;
    if (waveOutReset(output) != MMSYSERR_NOERROR) return 16;
    if (callback_count != 1 || callback_order != 1 || !(header.dwFlags & WHDR_DONE) || (header.dwFlags & WHDR_INQUEUE)) return 17;
    if (waveOutUnprepareHeader(output, &header, sizeof(header)) != MMSYSERR_NOERROR) return 18;
    if (waveOutReset(output) != MMSYSERR_NOERROR || waveOutClose(output) != MMSYSERR_NOERROR) return 19;
    HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!event) return 20;
    output = nullptr;
    if (waveOutOpen(&output, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(event), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR ||
        WaitForSingleObject(event, 0) != WAIT_OBJECT_0 || !ResetEvent(event)) return 21;
    WAVEHDR event_header{}; event_header.lpData = samples; event_header.dwBufferLength = sizeof(samples);
    if (waveOutPrepareHeader(output, &event_header, sizeof(event_header)) != MMSYSERR_NOERROR ||
        waveOutWrite(output, &event_header, sizeof(event_header)) != MMSYSERR_NOERROR ||
        waveOutReset(output) != MMSYSERR_NOERROR || WaitForSingleObject(event, 0) != WAIT_OBJECT_0 ||
        !(event_header.dwFlags & WHDR_DONE) || waveOutUnprepareHeader(output, &event_header, sizeof(event_header)) != MMSYSERR_NOERROR ||
        waveOutClose(output) != MMSYSERR_NOERROR || WaitForSingleObject(event, 0) != WAIT_OBJECT_0) return 22;
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
    puts("X86-WAVEOUT PASS: function, event, and window callbacks; WHDR_DONE ordering, advancing position, reset flush, close semantics");
    return 0;
}
#else
#include <winsock2.h>
#include <windows.h>
#include <cstdio>
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

int main(int argc, char **argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: x86-waveout-integration <dsound.dll> <guest.exe>\n"); return 2; }
    HMODULE shim = LoadLibraryA(argv[1]);
    if (!shim) { std::fprintf(stderr, "FAIL: LoadLibrary dsound shim: %lu\n", GetLastError()); return 2; }
    if (!shim_export(shim, "XbnWaveOutOpen")) { std::fprintf(stderr, "FAIL: internal waveOutOpen export missing\n"); return 2; }
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
