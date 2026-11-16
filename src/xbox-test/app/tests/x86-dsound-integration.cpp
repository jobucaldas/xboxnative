// SPDX-License-Identifier: GPL-3.0-or-later
// PE32 DirectSound guest fixture and Wine host runner. The guest calls the real
// XAudio2-backed dsound.dll only through the x86 COM marshallers.
#ifdef X86_DSOUND_GUEST
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <initguid.h>
#include <dsound.h>
#include <stdio.h>
#include <string.h>

int main() {
    IDirectSound8 *device = nullptr;
    HRESULT hr = DirectSoundCreate8(nullptr, &device, nullptr);
    if (FAILED(hr) || !device) return 10;
    if (FAILED(device->SetCooperativeLevel(reinterpret_cast<HWND>(1), DSSCL_PRIORITY))) return 11;

    DSBUFFERDESC primary_desc{};
    primary_desc.dwSize = sizeof(primary_desc);
    primary_desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
    IDirectSoundBuffer *primary = nullptr;
    if (FAILED(device->CreateSoundBuffer(&primary_desc, &primary, nullptr))) return 12;
    WAVEFORMATEX format{WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
    if (FAILED(primary->SetFormat(&format))) return 13;

    DSBUFFERDESC desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY |
                   DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GETCURRENTPOSITION2;
    desc.dwBufferBytes = 4096;
    desc.lpwfxFormat = &format;
    IDirectSoundBuffer *buffer = nullptr;
    if (FAILED(device->CreateSoundBuffer(&desc, &buffer, nullptr))) return 14;

    IDirectSoundBuffer8 *buffer8 = nullptr;
    if (FAILED(buffer->QueryInterface(IID_IDirectSoundBuffer8, reinterpret_cast<void **>(&buffer8))) || !buffer8) return 15;
    IUnknown *identity = nullptr;
    if (FAILED(buffer->QueryInterface(IID_IUnknown, reinterpret_cast<void **>(&identity))) || !identity) return 16;
    identity->Release();
    IDirectSoundNotify *notify = nullptr;
    if (FAILED(buffer->QueryInterface(IID_IDirectSoundNotify, reinterpret_cast<void **>(&notify))) || !notify) return 30;

    DSBUFFERDESC no_notify_desc = desc;
    no_notify_desc.dwFlags &= ~DSBCAPS_CTRLPOSITIONNOTIFY;
    IDirectSoundBuffer *no_notify = nullptr;
    if (FAILED(device->CreateSoundBuffer(&no_notify_desc, &no_notify, nullptr))) return 17;
    IDirectSoundNotify *unexpected = nullptr;
    if (no_notify->QueryInterface(IID_IDirectSoundNotify, reinterpret_cast<void **>(&unexpected)) != E_NOINTERFACE || unexpected) return 18;
    no_notify->Release();

    void *first = nullptr, *second = nullptr;
    DWORD first_bytes = 0, second_bytes = 0;
    if (FAILED(buffer->Lock(4092, 16, &first, &first_bytes, &second, &second_bytes, 0))) return 19;
    if (first_bytes != 4 || second_bytes != 12 || !first || !second) return 20;
    memset(first, 0, first_bytes);
    memset(second, 0, second_bytes);
    if (FAILED(buffer->Unlock(first, first_bytes, second, second_bytes))) return 21;

    HANDLE event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!event) return 22;
    DSBPOSITIONNOTIFY position{1024, event};
    if (FAILED(notify->SetNotificationPositions(1, &position))) return 23;
    if (FAILED(buffer->Play(0, 0, DSBPLAY_LOOPING))) return 24;
    DWORD before = 0, after = 0;
    if (FAILED(buffer->GetCurrentPosition(&before, nullptr))) return 25;
    for (unsigned i = 0; i < 100; ++i) {
        WaitForSingleObject(event, 10);
        if (FAILED(buffer->GetCurrentPosition(&after, nullptr))) return 26;
        if (after != before) break;
    }
    if (after == before) return 27;

    if (FAILED(buffer->SetVolume(-1000)) || FAILED(buffer->SetPan(-500)) || FAILED(buffer->SetFrequency(44100))) return 28;
    LONG volume = 0, pan = 0;
    DWORD frequency = 0;
    if (FAILED(buffer->GetVolume(&volume)) || volume != -1000 ||
        FAILED(buffer->GetPan(&pan)) || pan != -500 ||
        FAILED(buffer->GetFrequency(&frequency)) || frequency != 44100) return 29;
    if (WaitForSingleObject(event, 2000) != WAIT_OBJECT_0) return 31;
    if (FAILED(buffer->Stop())) return 32;

    notify->Release();
    buffer8->Release();
    buffer->Release();
    primary->Release();
    device->Release();
    CloseHandle(event);
    puts("X86-DSOUND PASS: create, wrap lock, looping cursor, controls, notify QI/event, releases");
    return 0;
}
#else
#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <initguid.h>
#include "../host/x86/x86_play.h"

static bool audit_dsound_slots() {
    // Expected DWORD stack slots (including this) in the MinGW dsound.h order.
    static constexpr unsigned device_arities[] = {3,1,1,4,2,3,3,1,2,2,2,2};
    static constexpr unsigned buffer_arities[] = {3,1,1,2,3,4,2,2,2,2,3,8,4,2,2,2,2,2,1,5,1,4,4,5};
    static constexpr unsigned notify_arities[] = {3,1,1,3};
    struct Expected { const char *name; const unsigned *arities; unsigned slots; };
    static constexpr Expected expected[] = {
        {"IDirectSound8", device_arities, sizeof(device_arities) / sizeof(device_arities[0])},
        {"IDirectSoundBuffer8", buffer_arities, sizeof(buffer_arities) / sizeof(buffer_arities[0])},
        {"IDirectSoundNotify", notify_arities, sizeof(notify_arities) / sizeof(notify_arities[0])},
    };
    x86::Host host;
    x86::register_dsound_com(host.com, host);
    for (const auto &item : expected) {
        const auto &iface = host.com.by_name(item.name);
        if (iface.slots != item.slots) {
            std::fprintf(stderr, "DirectSound slot audit: %s has %u slots, expected %u\\n", item.name, iface.slots, item.slots);
            return false;
        }
        for (unsigned slot = 3; slot < iface.slots; ++slot) {
            auto method = iface.methods.find(slot);
            if (method == iface.methods.end() || method->second.argc != item.arities[slot]) {
                std::fprintf(stderr, "DirectSound slot audit: %s slot=%u expected argc=%u actual=%u\\n", item.name, slot,
                             item.arities[slot], method == iface.methods.end() ? 0 : method->second.argc);
                return false;
            }
        }
    }
    return true;
}

int main(int argc, char **argv) {
    if (!audit_dsound_slots()) {
        std::fprintf(stderr, "X86-DSOUND FAIL: DirectSound slot/arity audit against dsound.h\\n");
        return 1;
    }
    if (argc != 3) { std::fprintf(stderr, "usage: x86-dsound-integration <dsound.dll> <guest.exe>\n"); return 2; }
    HMODULE shim = LoadLibraryA(argv[1]);
    if (!shim) { std::fprintf(stderr, "FAIL: LoadLibrary dsound shim: %lu\n", GetLastError()); return 2; }
    std::ifstream f(argv[2], std::ios::binary);
    if (!f) { std::fprintf(stderr, "FAIL: cannot open guest fixture\n"); return 2; }
    std::vector<uint8_t> image{std::istreambuf_iterator<char>(f), {}};
    std::string output;
    auto result = x86::run_image(image, "dsound-fixture.exe", "dsound-fixture.exe", [&output](const std::string &s) { output += s; }, nullptr,
        500ull * 1000 * 1000, nullptr, false, nullptr, nullptr, [shim](x86::Host &host) {
            host.dsound_factory = [shim](const void *id, uintptr_t *out) -> uintptr_t {
                using Create = HRESULT (WINAPI *)(LPCGUID, LPDIRECTSOUND8 *, LPUNKNOWN);
                auto create = reinterpret_cast<Create>(GetProcAddress(shim, "DirectSoundCreate8"));
                if (!out || !create) return E_NOINTERFACE;
                *out = 0;
                IDirectSound8 *device = nullptr;
                HRESULT hr = create(static_cast<LPCGUID>(id), &device, nullptr);
                *out = reinterpret_cast<uintptr_t>(device);
                return static_cast<uint32_t>(hr);
            };
        });
    if (!result.fault.empty()) std::fprintf(stderr, "X86-DSOUND FAIL: %s: %s\n", result.fault_kind.c_str(), result.fault.c_str());
    if (!output.empty()) std::fwrite(output.data(), 1, output.size(), stdout);
    bool passed = result.loaded && result.finished && result.exit_code == 0 &&
                  output.find("X86-DSOUND PASS:") != std::string::npos;
    std::printf("X86-DSOUND %s: interpreter loaded=%d finished=%d exit=%u imports=%u bound=%u\n",
                passed ? "PASS" : "FAIL", result.loaded, result.finished, result.exit_code, result.imports, result.bound);
    FreeLibrary(shim);
    return passed ? 0 : 1;
}
#endif
