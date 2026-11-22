// SPDX-License-Identifier: GPL-3.0-or-later
// PE32 DirectSound guest fixture and Wine host runner. The guest calls the real
// XAudio2-backed dsound.dll only through the x86 COM marshallers.
#ifdef X86_DSOUND_GUEST
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <initguid.h>
#include <dsound.h>
#include <stdio.h>
#include <string.h>

int main() {
    IDirectSound8 *device = nullptr;
    HRESULT hr = DirectSoundCreate8(nullptr, &device, nullptr);
    if (FAILED(hr) || !device) return 10;
    if (device->Initialize(nullptr) != DSERR_ALREADYINITIALIZED) return 33;
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

    HANDLE other = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    HANDLE tail = CreateEventA(nullptr, FALSE, TRUE, nullptr);
    HANDLE tail2 = CreateEventA(nullptr, FALSE, TRUE, nullptr);
    if (!other || !tail || !tail2) return 33;
    // Wait-any with two signaled auto-reset events must consume only index 0.
    HANDLE auto_pair[2] = {tail, tail2};
    if (WaitForMultipleObjects(2, auto_pair, FALSE, 0) != WAIT_OBJECT_0) return 34;
    if (WaitForSingleObject(tail, 0) != WAIT_TIMEOUT) return 35;
    if (WaitForSingleObject(tail2, 0) != WAIT_OBJECT_0) return 36;
    // Wait-all failing with an unsignaled handle must not consume the signaled auto-reset event.
    if (!SetEvent(tail)) return 37;
    HANDLE fail_all[2] = {other, tail};
    if (WaitForMultipleObjects(2, fail_all, TRUE, 0) != WAIT_TIMEOUT) return 38;
    if (WaitForSingleObject(tail, 0) != WAIT_OBJECT_0) return 39;
    // Blocking WaitForMultipleObjects (finite and INFINITE) woken by the DirectSound notify event.
    if (!ResetEvent(event)) return 40;
    HANDLE notify_any[2] = {other, event};
    if (WaitForMultipleObjects(2, notify_any, FALSE, 2000) != WAIT_OBJECT_0 + 1) return 41;
    if (!ResetEvent(event)) return 42;
    if (WaitForMultipleObjects(2, notify_any, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) return 43;
    // Blocking atomic WaitForMultipleObjects (bWaitAll=TRUE) woken by the DirectSound notify event.
    if (!ResetEvent(event) || !SetEvent(tail)) return 44;
    HANDLE notify_all[2] = {tail, event};
    if (WaitForMultipleObjects(2, notify_all, TRUE, 2000) != WAIT_OBJECT_0) return 45;
    if (WaitForSingleObject(tail, 0) != WAIT_TIMEOUT) return 46;

    if (FAILED(buffer->Stop())) return 32;

    notify->Release();
    buffer8->Release();
    buffer->Release();
    primary->Release();
    device->Release();
    CloseHandle(tail2);
    CloseHandle(tail);
    CloseHandle(other);
    CloseHandle(event);
    // Deferred COM path: CoCreateInstance(CLSID_DirectSound / CLSID_DirectSound8) must
    // return an uninitialized proxy whose host device is created only in Initialize().
    if (FAILED(CoInitialize(nullptr))) return 50;
    IDirectSound *uninit_ds = nullptr;
    if (FAILED(CoCreateInstance(CLSID_DirectSound, nullptr, CLSCTX_INPROC_SERVER, IID_IDirectSound, reinterpret_cast<void **>(&uninit_ds))) || !uninit_ds) return 51;
    if (uninit_ds->Compact() != DSERR_UNINITIALIZED) return 52;
    if (uninit_ds->Release() != 0) return 53;

    IUnknown *com_unk = nullptr;
    if (FAILED(CoCreateInstance(CLSID_DirectSound8, nullptr, CLSCTX_INPROC_SERVER, IID_IUnknown, reinterpret_cast<void **>(&com_unk))) || !com_unk) return 54;
    IDirectSound8 *com_ds8 = nullptr;
    if (FAILED(com_unk->QueryInterface(IID_IDirectSound8, reinterpret_cast<void **>(&com_ds8))) || !com_ds8) return 55;
    IDirectSound *com_ds = nullptr;
    if (FAILED(com_unk->QueryInterface(IID_IDirectSound, reinterpret_cast<void **>(&com_ds))) || !com_ds) return 56;

    IDirectSoundBuffer *pre_buf = reinterpret_cast<IDirectSoundBuffer *>(1);
    if (com_ds8->CreateSoundBuffer(&desc, &pre_buf, nullptr) != DSERR_UNINITIALIZED || pre_buf != nullptr) return 57;
    DSCAPS caps{};
    caps.dwSize = sizeof(caps);
    if (com_ds8->GetCaps(&caps) != DSERR_UNINITIALIZED) return 58;
    IDirectSoundBuffer *pre_dup = reinterpret_cast<IDirectSoundBuffer *>(1);
    if (com_ds8->DuplicateSoundBuffer(nullptr, &pre_dup) != DSERR_UNINITIALIZED || pre_dup != nullptr) return 59;
    if (com_ds8->SetCooperativeLevel(reinterpret_cast<HWND>(1), DSSCL_PRIORITY) != DSERR_UNINITIALIZED) return 60;
    if (com_ds8->Compact() != DSERR_UNINITIALIZED) return 61;
    DWORD spk = 0, cert = 0;
    if (com_ds8->GetSpeakerConfig(&spk) != DSERR_UNINITIALIZED) return 62;
    if (com_ds8->SetSpeakerConfig(DSSPEAKER_STEREO) != DSERR_UNINITIALIZED) return 63;
    if (com_ds8->VerifyCertification(&cert) != DSERR_UNINITIALIZED) return 64;

    const GUID bad_guid{0x12345678u, 0x1234u, 0x5678u, {1, 2, 3, 4, 5, 6, 7, 8}};
    if (com_ds8->Initialize(&bad_guid) != DSERR_NODRIVER) return 65;
    if (com_ds8->Compact() != DSERR_UNINITIALIZED) return 66;

    if (FAILED(com_ds8->Initialize(nullptr))) return 67;
    if (com_ds->Initialize(nullptr) != DSERR_ALREADYINITIALIZED) return 68;
    if (com_ds8->Initialize(nullptr) != DSERR_ALREADYINITIALIZED) return 69;

    IUnknown *com_unk2 = nullptr;
    if (FAILED(com_ds8->QueryInterface(IID_IUnknown, reinterpret_cast<void **>(&com_unk2))) || com_unk2 != com_unk) return 70;
    if (FAILED(com_ds8->SetCooperativeLevel(reinterpret_cast<HWND>(1), DSSCL_PRIORITY))) return 71;
    if (FAILED(com_ds8->GetCaps(&caps)) || caps.dwFlags == 0) return 72;
    if (FAILED(com_ds8->Compact())) return 73;
    if (FAILED(com_ds8->GetSpeakerConfig(&spk)) || spk == 0) return 74;
    if (FAILED(com_ds8->VerifyCertification(&cert))) return 75;

    IDirectSoundBuffer *com_buf = nullptr;
    if (FAILED(com_ds8->CreateSoundBuffer(&desc, &com_buf, nullptr)) || !com_buf) return 76;
    void *c1 = nullptr, *c2 = nullptr;
    DWORD cn1 = 0, cn2 = 0;
    if (FAILED(com_buf->Lock(0, 256, &c1, &cn1, &c2, &cn2, 0)) || !c1 || cn1 != 256) return 77;
    memset(c1, 0, cn1);
    if (FAILED(com_buf->Unlock(c1, cn1, c2, cn2))) return 78;
    com_buf->Release();
    com_unk2->Release();
    com_ds->Release();
    com_ds8->Release();
    if (com_unk->Release() != 0) return 79;
    CoUninitialize();

    puts("X86-DSOUND PASS: create, wrap lock, looping cursor, controls, notify QI/event, WFMO, CoCreateInstance deferred init, releases");
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
    if (!GetProcAddress(shim, "XbnWaveOutGetNumDevs")) {
        std::fprintf(stderr, "FAIL: loaded DLL is not Play's DirectSound shim (%s)\n", argv[1]);
        return 2;
    }
    std::ifstream f(argv[2], std::ios::binary);
    if (!f) { std::fprintf(stderr, "FAIL: cannot open guest fixture\n"); return 2; }
    std::vector<uint8_t> image{std::istreambuf_iterator<char>(f), {}};
    std::string output;
    unsigned factory_calls = 0;
    auto result = x86::run_image(image, "dsound-fixture.exe", "dsound-fixture.exe", [&output](const std::string &s) { output += s; }, nullptr,
        500ull * 1000 * 1000, nullptr, false, nullptr, nullptr, [shim, &factory_calls](x86::Host &host) {
            host.dsound_factory = [shim, &factory_calls](const void *id, uintptr_t *out) -> uintptr_t {
                ++factory_calls;
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
    // Expected factory invocations: DirectSoundCreate8 (1), failed Initialize(&bad_guid) (1),
    // and successful Initialize(nullptr) (1). Neither CoCreateInstance nor double-Initialize calls it.
    bool passed = result.loaded && result.finished && result.exit_code == 0 &&
                  factory_calls == 3 &&
                  output.find("X86-DSOUND PASS:") != std::string::npos;
    std::printf("X86-DSOUND %s: interpreter loaded=%d finished=%d exit=%u imports=%u bound=%u factory_calls=%u\n",
                passed ? "PASS" : "FAIL", result.loaded, result.finished, result.exit_code, result.imports, result.bound, factory_calls);
    FreeLibrary(shim);
    return passed ? 0 : 1;
}
#endif
