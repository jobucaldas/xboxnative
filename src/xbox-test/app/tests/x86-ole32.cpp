// SPDX-License-Identifier: GPL-3.0-or-later
// Focused guest OLE32 route test. It exercises guest-visible allocation,
// apartment, GUID, DirectSound class-boundary, and PROPVARIANT contracts.
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../host/x86/x86_guest.h"
using namespace x86;

static int checks, failures;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #value); } } while (0)

int main() {
    Memory mem; mem.map(0x1000, 0x30000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.cur_teb = 0x1000; guest.register_all();
    Cpu cpu(mem, table); cpu.reg[ESP] = 0x2f000;
    auto route = [&](const char* name, unsigned argc) -> Thunk* {
        auto it = reg.map.find({"ole32.dll", Registry::key_sym(name)});
        CHECK(it != reg.map.end());
        if (it == reg.map.end()) return nullptr;
        CHECK(it->second.conv == Conv::Stdcall); CHECK(it->second.argc == argc); return &it->second;
    };
    auto call = [&](const char* name, std::initializer_list<uint32_t> args) -> uint32_t {
        unsigned index = 0; for (uint32_t value : args) mem.w32(cpu.reg[ESP] + 4 + 4 * index++, value);
        return reg.map.at({"ole32.dll", Registry::key_sym(name)}).fn(cpu);
    };
    route("CoInitialize", 1); route("CoInitializeEx", 2); route("CoUninitialize", 0);
    route("CoTaskMemAlloc", 1); route("CoTaskMemFree", 1); route("CoTaskMemRealloc", 2);
    route("CoCreateGuid", 1); route("StringFromGUID2", 3); route("CLSIDFromString", 2); route("IIDFromString", 2);
    route("CoCreateInstance", 5); route("OleInitialize", 1); route("OleUninitialize", 0); route("PropVariantClear", 1);

    host.random_bytes = [](uint8_t* output, uint32_t size) { for (uint32_t i = 0; i < size; ++i) output[i] = uint8_t(i + 1); return true; };
    uint32_t allocation = call("CoTaskMemAlloc", {8}); CHECK(allocation != 0); mem.w32(allocation, 0x12345678u);
    uint32_t larger = call("CoTaskMemRealloc", {allocation, 16}); CHECK(larger != 0 && mem.r32(larger) == 0x12345678u);
    CHECK(call("CoTaskMemRealloc", {larger, 0}) == 0); CHECK(call("CoTaskMemFree", {0}) == 0);

    uint32_t guid = 0x4000, text = 0x4100, parsed = 0x4200;
    CHECK(call("CoCreateGuid", {guid}) == 0);
    CHECK(mem.r8(guid + 6) == 0x07 && mem.r8(guid + 7) == 0x48 && mem.r8(guid + 8) == 0x89);
    CHECK(call("StringFromGUID2", {guid, text, 39}) == 39);
    CHECK(guest.wstr(text) == u"{04030201-0605-4807-890A-0B0C0D0E0F10}");
    uint32_t input = guest.put_wstr(guest.wstr(text));
    CHECK(call("CLSIDFromString", {input, parsed}) == 0);
    for (unsigned i = 0; i < 16; ++i) CHECK(mem.r8(guid + i) == mem.r8(parsed + i));
    CHECK(call("IIDFromString", {guest.put_wstr(u"{not-a-guid}"), parsed}) == 0x800401f3u);
    CHECK(call("StringFromGUID2", {guid, text, 38}) == 0);

    mem.w32(guest.cur_teb + 0x24, 55);
    CHECK(call("CoInitialize", {0}) == 0); CHECK(call("CoInitialize", {0}) == 1);
    CHECK(call("OleInitialize", {0}) == 1); CHECK(call("CoInitializeEx", {0, 0}) == 0x80010106u);
    CHECK(call("CoInitializeEx", {0, 2}) == 1); CHECK(call("CoInitializeEx", {0, 0x40}) == 0x80070057u);
    call("OleUninitialize", {}); call("CoUninitialize", {}); call("CoUninitialize", {}); call("CoUninitialize", {});
    CHECK(guest.com_apartments.count(55) == 0);

    const uint8_t zero_guid[16] = {};
    mem.write_bytes(0x4300, zero_guid, 16); const uint8_t iid_unknown[16] = {0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
    mem.write_bytes(0x4400, iid_unknown, 16); mem.w32(0x4500, 0xfeedbeefu);
    CHECK(call("CoCreateInstance", {0x4300, 0, 0x17, 0x4400, 0x4500}) == 0x80040154u && mem.r32(0x4500) == 0);
    const uint8_t clsid_sound8[16] = {0x3f,0xcc,0x01,0x39,0xb5,0x84,0xa4,0x4f,0xba,0x35,0xaa,0x81,0x72,0xb8,0xa0,0x9b};
    const uint8_t clsid_sound[16] = {0x46,0xd9,0xd4,0x47,0xe8,0x62,0xcf,0x11,0x93,0xbc,0x44,0x45,0x53,0x54,0x00,0x00};
    const uint8_t iid_sound[16] = {0x83,0xfa,0x9a,0x27,0x81,0x49,0xce,0x11,0xa5,0x21,0,0x20,0xaf,0x0b,0xe5,0x60};
    const uint8_t iid_sound8[16] = {0x93,0x7e,0x0a,0xc5,0x95,0xf3,0x34,0x48,0x9e,0xf6,0x7f,0xa9,0x9d,0xe5,0x09,0x66};
    const uint8_t iid_bogus[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    mem.write_bytes(0x4300, clsid_sound8, 16); uint32_t sound_hr = call("CoCreateInstance", {0x4300, 0, 0x17, 0x4400, 0x4500});
    CHECK(sound_hr == 0x80004001u); CHECK(mem.r32(0x4500) == 0);
    mem.write_bytes(0x4440, iid_sound, 16); mem.write_bytes(0x4460, iid_sound8, 16);
    mem.write_bytes(0x4300, clsid_sound, 16); mem.w32(0x4500, 0xdeadbeefu);
    CHECK(call("CoCreateInstance", {0x4300, 0, 0x17, 0x4440, 0x4500}) == 0x80004001u && mem.r32(0x4500) == 0);
    mem.write_bytes(0x4300, clsid_sound8, 16); mem.w32(0x4500, 0xdeadbeefu);
    CHECK(call("CoCreateInstance", {0x4300, 0, 0x17, 0x4460, 0x4500}) == 0x80004001u && mem.r32(0x4500) == 0);
    mem.write_bytes(0x4420, iid_bogus, 16); mem.w32(0x4500, 0xdeadbeefu);
    CHECK(call("CoCreateInstance", {0x4300, 0, 0x17, 0x4420, 0x4500}) == 0x80004002u && mem.r32(0x4500) == 0);
    mem.w32(0x4500, 0xdeadbeefu);
    CHECK(call("CoCreateInstance", {0x4300, 1, 0x17, 0x4400, 0x4500}) == 0x80040110u && mem.r32(0x4500) == 0);
    mem.w32(0x4500, 0xdeadbeefu);
    CHECK(call("CoCreateInstance", {0, 0, 0x17, 0x4400, 0x4500}) == 0x80004003u && mem.r32(0x4500) == 0);
    CHECK(call("CoCreateInstance", {0x4300, 0, 0x17, 0x4400, 0}) == 0x80004003u);

    mem.w(0x4600, 3, 2); CHECK(call("PropVariantClear", {0x4600}) == 0 && mem.r(0x4600, 2) == 0);
    mem.w(0x4600, 8, 2); CHECK(call("PropVariantClear", {0x4600}) == 0x80004001u && mem.r(0x4600, 2) == 8);
    CHECK(call("PropVariantClear", {0}) == 0x80004003u);

#if defined(_WIN32)
    struct MockDirectSound8 final : public IDirectSound8 {
        LONG refs = 1;
        unsigned* destroyed_counter = nullptr;
        unsigned set_coop_calls = 0, compact_calls = 0, get_caps_calls = 0;
        unsigned get_speaker_calls = 0, set_speaker_calls = 0, verify_cert_calls = 0;
        unsigned create_buf_calls = 0, dup_buf_calls = 0, init_calls = 0, qi_calls = 0;
        explicit MockDirectSound8(unsigned* destroyed) : destroyed_counter(destroyed) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
            ++qi_calls;
            if (!ppv) return E_POINTER;
            *ppv = nullptr;
            if (IsEqualIID(riid, dsound_iid::unknown) || IsEqualIID(riid, dsound_iid::sound) || IsEqualIID(riid, dsound_iid::sound8)) {
                *ppv = static_cast<IDirectSound8*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(++refs); }
        ULONG STDMETHODCALLTYPE Release() override {
            ULONG left = static_cast<ULONG>(--refs);
            if (left == 0) {
                if (destroyed_counter) ++(*destroyed_counter);
                delete this;
            }
            return left;
        }
        HRESULT STDMETHODCALLTYPE CreateSoundBuffer(LPCDSBUFFERDESC, LPLPDIRECTSOUNDBUFFER out, IUnknown*) override {
            ++create_buf_calls;
            if (out) *out = nullptr;
            return DSERR_UNSUPPORTED;
        }
        HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS caps) override {
            ++get_caps_calls;
            if (!caps || caps->dwSize < sizeof(DSCAPS)) return DSERR_INVALIDPARAM;
            caps->dwFlags = 0x12345678u;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER, LPLPDIRECTSOUNDBUFFER out) override {
            ++dup_buf_calls;
            if (out) *out = nullptr;
            return DSERR_UNSUPPORTED;
        }
        HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD) override { ++set_coop_calls; return S_OK; }
        HRESULT STDMETHODCALLTYPE Compact() override { ++compact_calls; return S_OK; }
        HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD out) override {
            ++get_speaker_calls;
            if (!out) return DSERR_INVALIDPARAM;
            *out = 0x00020004u;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD) override { ++set_speaker_calls; return S_OK; }
        HRESULT STDMETHODCALLTYPE Initialize(LPCGUID) override { ++init_calls; return DSERR_ALREADYINITIALIZED; }
        HRESULT STDMETHODCALLTYPE VerifyCertification(LPDWORD out) override {
            ++verify_cert_calls;
            if (!out) return DSERR_INVALIDPARAM;
            *out = DS_UNCERTIFIED;
            return S_OK;
        }
    };

    map_thunk_page(mem, table);
    auto com_call = [&](uint32_t proxy, unsigned slot, auto... rest) -> uint32_t {
        uint32_t vtable = mem.r32(proxy);
        uint32_t fn = mem.r32(vtable + slot * 4);
        return cpu.call(fn, {proxy, static_cast<uint32_t>(rest)...});
    };
    auto call_ds = [&](const char* name, std::initializer_list<uint32_t> args) -> uint32_t {
        unsigned index = 0; for (uint32_t value : args) mem.w32(cpu.reg[ESP] + 4 + 4 * index++, value);
        return reg.map.at({"dsound.dll", Registry::key_sym(name)}).fn(cpu);
    };

    unsigned factory_calls = 0, destroyed = 0;
    bool factory_should_fail = false, factory_return_null = false, last_had_guid = false;
    GUID last_guid{};
    MockDirectSound8* last_mock = nullptr;
    host.dsound_factory = [&](const void* id, uintptr_t* out) -> uintptr_t {
        ++factory_calls;
        last_had_guid = (id != nullptr);
        if (id) std::memcpy(&last_guid, id, sizeof(last_guid));
        else std::memset(&last_guid, 0, sizeof(last_guid));
        if (!out) return E_POINTER;
        *out = 0;
        if (factory_should_fail) return DSERR_NODRIVER_X86;
        if (factory_return_null) return S_OK;
        last_mock = new MockDirectSound8(&destroyed);
        *out = reinterpret_cast<uintptr_t>(static_cast<IDirectSound8*>(last_mock));
        return S_OK;
    };

    mem.write_bytes(0x4300, clsid_sound8, 16);
    mem.write_bytes(0x4320, clsid_sound, 16);
    mem.write_bytes(0x4440, iid_sound, 16);
    mem.write_bytes(0x4460, iid_sound8, 16);

    // 1. CoCreateInstance(CLSID_DirectSound8) returns deferred proxy without calling dsound_factory.
    mem.w32(0x4500, 0);
    CHECK(call("CoCreateInstance", {0x4300, 0, 0x17, 0x4460, 0x4500}) == 0);
    uint32_t ds8 = mem.r32(0x4500);
    CHECK(ds8 != 0 && factory_calls == 0 && host.com.live() == 1);

    // 2. Second concurrent CoCreateInstance(CLSID_DirectSound) gets a distinct proxy and releases cleanly without init.
    mem.w32(0x4504, 0);
    CHECK(call("CoCreateInstance", {0x4320, 0, 0x17, 0x4440, 0x4504}) == 0);
    uint32_t ds_uninit = mem.r32(0x4504);
    CHECK(ds_uninit != 0 && ds_uninit != ds8 && factory_calls == 0 && host.com.live() == 2);
    CHECK(com_call(ds_uninit, 1) == 2); // AddRef
    CHECK(com_call(ds_uninit, 2) == 1); // Release
    CHECK(com_call(ds_uninit, 2) == 0); // Release -> destroyed cleanly without init
    CHECK(factory_calls == 0 && destroyed == 0 && host.com.live() == 1);

    // 3. All operational IDirectSound8 methods return DSERR_UNINITIALIZED before Initialize.
    uint32_t buf_out = 0x4700, caps_addr = 0x4720, spk_out = 0x4800, cert_out = 0x4804, desc_addr = 0x4820;
    mem.w32(buf_out, 0x11111111u);
    mem.w32(desc_addr, 36);
    CHECK(com_call(ds8, 3, desc_addr, buf_out, 0) == DSERR_UNINITIALIZED_X86 && mem.r32(buf_out) == 0);
    CHECK(com_call(ds8, 4, caps_addr) == DSERR_UNINITIALIZED_X86);
    mem.w32(buf_out, 0x22222222u);
    CHECK(com_call(ds8, 5, 0, buf_out) == DSERR_UNINITIALIZED_X86 && mem.r32(buf_out) == 0);
    CHECK(com_call(ds8, 6, 1, 2) == DSERR_UNINITIALIZED_X86);
    CHECK(com_call(ds8, 7) == DSERR_UNINITIALIZED_X86);
    CHECK(com_call(ds8, 8, spk_out) == DSERR_UNINITIALIZED_X86);
    CHECK(com_call(ds8, 9, 2) == DSERR_UNINITIALIZED_X86);
    CHECK(com_call(ds8, 11, cert_out) == DSERR_UNINITIALIZED_X86);
    CHECK(factory_calls == 0);

    // 4. QueryInterface before Initialize returns the same proxy identity and shares state.
    uint32_t qi_out = 0x4840;
    CHECK(com_call(ds8, 0, 0x4400, qi_out) == 0 && mem.r32(qi_out) == ds8); // IID_IUnknown
    uint32_t unk = mem.r32(qi_out);
    CHECK(com_call(ds8, 0, 0x4440, qi_out) == 0 && mem.r32(qi_out) == ds8); // IID_IDirectSound
    uint32_t ds1 = mem.r32(qi_out);
    mem.w32(qi_out, 0x33333333u);
    CHECK(com_call(ds8, 0, 0x4420, qi_out) == 0x80004002u && mem.r32(qi_out) == 0); // bogus IID
    CHECK(factory_calls == 0);

    // 5. Initialize forwards device GUID and keeps proxy uninitialized on factory failure or null result.
    const GUID test_guid{0x11223344u, 0x5566u, 0x7788u, {1, 2, 3, 4, 5, 6, 7, 8}};
    uint32_t guid_addr = 0x4860;
    mem.write_bytes(guid_addr, &test_guid, sizeof(test_guid));
    factory_should_fail = true;
    CHECK(com_call(ds1, 10, guid_addr) == DSERR_NODRIVER_X86);
    CHECK(factory_calls == 1 && last_had_guid && std::memcmp(&last_guid, &test_guid, sizeof(test_guid)) == 0);
    CHECK(com_call(ds8, 7) == DSERR_UNINITIALIZED_X86);
    factory_should_fail = false;
    factory_return_null = true;
    CHECK(com_call(ds1, 10, guid_addr) == DSERR_NODRIVER_X86);
    CHECK(factory_calls == 2 && com_call(ds8, 7) == DSERR_UNINITIALIZED_X86);

    // 6. Initialize succeeds on retry; state is shared across QI'd pointers; second Initialize returns DSERR_ALREADYINITIALIZED.
    factory_return_null = false;
    CHECK(com_call(ds1, 10, guid_addr) == 0);
    CHECK(factory_calls == 3 && last_had_guid && std::memcmp(&last_guid, &test_guid, sizeof(test_guid)) == 0);
    CHECK(com_call(ds8, 10, 0) == DSERR_ALREADYINITIALIZED_X86);
    CHECK(com_call(unk, 10, guid_addr) == DSERR_ALREADYINITIALIZED_X86);
    CHECK(factory_calls == 3 && last_mock && last_mock->init_calls == 0);

    // 7. Post-Initialize operational methods delegate to the host IDirectSound8 and post-init QI keeps identity.
    CHECK(com_call(ds8, 6, 1, 2) == 0 && last_mock->set_coop_calls == 1);
    CHECK(com_call(ds1, 7) == 0 && last_mock->compact_calls == 1);
    CHECK(com_call(ds8, 4, caps_addr) == 0 && last_mock->get_caps_calls == 1 && mem.r32(caps_addr + 4) == 0x12345678u);
    CHECK(com_call(ds8, 8, spk_out) == 0 && last_mock->get_speaker_calls == 1 && mem.r32(spk_out) == 0x00020004u);
    CHECK(com_call(ds8, 9, 2) == 0 && last_mock->set_speaker_calls == 1);
    CHECK(com_call(ds8, 11, cert_out) == 0 && last_mock->verify_cert_calls == 1 && mem.r32(cert_out) == DS_UNCERTIFIED);
    CHECK(com_call(ds8, 0, 0x4400, qi_out) == 0 && mem.r32(qi_out) == unk);
    uint32_t unk_after = mem.r32(qi_out);
    CHECK(com_call(ds8, 0, 0x4420, qi_out) == 0x80004002u && mem.r32(qi_out) == 0);
    CHECK(com_call(unk_after, 2) == 3);
    CHECK(com_call(ds1, 2) == 2);
    CHECK(com_call(unk, 2) == 1);
    CHECK(destroyed == 0);
    CHECK(com_call(ds8, 2) == 0);
    CHECK(destroyed == 1 && host.com.live() == 0);

    // 8. Initialize(nullptr) forwards nullptr GUID to dsound_factory.
    CHECK(call("CoCreateInstance", {0x4300, 0, 0x17, 0x4400, 0x4500}) == 0);
    uint32_t ds_null_guid = mem.r32(0x4500);
    CHECK(ds_null_guid != 0 && factory_calls == 3);
    CHECK(com_call(ds_null_guid, 10, 0) == 0 && factory_calls == 4 && !last_had_guid);
    CHECK(com_call(ds_null_guid, 2) == 0 && destroyed == 2 && host.com.live() == 0);

    // 9. DirectSoundCreate8 and DirectSoundCreate initialize eagerly and reject subsequent Initialize.
    mem.w32(0x4508, 0);
    CHECK(call_ds("DirectSoundCreate8", {0, 0x4508, 0}) == 0);
    uint32_t eager8 = mem.r32(0x4508);
    CHECK(eager8 != 0 && factory_calls == 5 && !last_had_guid);
    CHECK(com_call(eager8, 7) == 0 && last_mock->compact_calls == 1);
    CHECK(com_call(eager8, 10, 0) == DSERR_ALREADYINITIALIZED_X86 && factory_calls == 5);
    CHECK(com_call(eager8, 2) == 0 && destroyed == 3 && host.com.live() == 0);

    mem.w32(0x450c, 0);
    CHECK(call_ds("DirectSoundCreate", {guid_addr, 0x450c, 0}) == 0);
    uint32_t eager1 = mem.r32(0x450c);
    CHECK(eager1 != 0 && factory_calls == 6 && last_had_guid && std::memcmp(&last_guid, &test_guid, sizeof(test_guid)) == 0);
    CHECK(com_call(eager1, 10, 0) == DSERR_ALREADYINITIALIZED_X86 && factory_calls == 6);
    CHECK(com_call(eager1, 2) == 0 && destroyed == 4 && host.com.live() == 0);

    // 10. Teardown destroys both live uninitialized and live initialized deferred proxies without leak or crash.
    {
        Memory tmem; tmem.map(0x1000, 0x30000, PROT_R | PROT_W); tmem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
        ThunkTable ttable; map_thunk_page(tmem, ttable);
        Host thost; Registry treg; Guest tguest(tmem, thost, treg); tguest.cur_teb = 0x1000; tguest.register_all();
        Cpu tcpu(tmem, ttable); tcpu.reg[ESP] = 0x2f000;
        thost.dsound_factory = [&](const void*, uintptr_t* out) -> uintptr_t {
            *out = reinterpret_cast<uintptr_t>(static_cast<IDirectSound8*>(new MockDirectSound8(&destroyed)));
            return S_OK;
        };
        tmem.write_bytes(0x4300, clsid_sound8, 16); tmem.write_bytes(0x4460, iid_sound8, 16);
        auto tcall = [&](std::initializer_list<uint32_t> args) -> uint32_t {
            unsigned i = 0; for (uint32_t v : args) tmem.w32(tcpu.reg[ESP] + 4 + 4 * i++, v);
            return treg.map.at({"ole32.dll", Registry::key_sym("CoCreateInstance")}).fn(tcpu);
        };
        CHECK(tcall({0x4300, 0, 0x17, 0x4460, 0x4500}) == 0 && tmem.r32(0x4500) != 0);
        CHECK(tcall({0x4300, 0, 0x17, 0x4460, 0x4504}) == 0 && tmem.r32(0x4504) != 0);
        uint32_t live_init = tmem.r32(0x4504);
        CHECK(tcpu.call(tmem.r32(tmem.r32(live_init) + 10 * 4), {live_init, 0}) == 0);
        CHECK(thost.com.live() == 2);
    }
    CHECK(destroyed == 5);
#endif

    std::printf("x86-ole32: %d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
