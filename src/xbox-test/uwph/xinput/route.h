#pragma once
#include <windows.h>
#include <xinput.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <functional>
#include <cstring>
#include <cstdint>
namespace xinput_core {
using Rumble=std::function<DWORD(WORD,WORD)>;
inline DWORD error_to_win32(HRESULT error) {
    if(SUCCEEDED(error))return ERROR_SUCCESS;
    if(error==E_ACCESSDENIED)return ERROR_ACCESS_DENIED;
    if(error==E_NOTIMPL)return ERROR_NOT_SUPPORTED;
    if(error==E_INVALIDARG)return ERROR_INVALID_PARAMETER;
    if(error==HRESULT(0x80000013L))return ERROR_DEVICE_NOT_CONNECTED; // RO_E_CLOSED
    if(HRESULT_FACILITY(error)==FACILITY_WIN32)return HRESULT_CODE(error);
    return ERROR_GEN_FAILURE;
}
struct Slot {
    bool connected=false;
    XINPUT_STATE state{};
    XINPUT_GAMEPAD hardware{};
    XINPUT_VIBRATION vibration{};
    Rumble rumble;
};
inline std::array<Slot,4> slots;
// Optional diagnostics sink (the launcher's guest log). Each distinct (call,
// slot, result) is reported once, so a console log shows whether the guest ever
// polls the emulated XInput and what it was told, without a line per frame.
inline void (*note)(const char*)=nullptr;
inline void trace(bool set,DWORD index,DWORD result) {
    if(!note || index>=slots.size())return;
    static std::atomic<unsigned> seen[2]{};
    const unsigned bit=1u<<(index*3+(result==ERROR_SUCCESS?0:result==ERROR_DEVICE_NOT_CONNECTED?1:2));
    if(seen[set].fetch_or(bit)&bit)return;
    char line[96];snprintf(line,sizeof line,"XINPUT XInput%sState slot=%lu result=%lu (first)",set?"Set":"Get",static_cast<unsigned long>(index),static_cast<unsigned long>(result));note(line);
}
inline std::mutex mutex;
inline bool enabled=true;
inline bool active=true;
inline bool equal(const XINPUT_GAMEPAD& a,const XINPUT_GAMEPAD& b) {
    return a.wButtons==b.wButtons && a.bLeftTrigger==b.bLeftTrigger &&
        a.bRightTrigger==b.bRightTrigger && a.sThumbLX==b.sThumbLX &&
        a.sThumbLY==b.sThumbLY && a.sThumbRX==b.sThumbRX && a.sThumbRY==b.sThumbRY;
}
// Called by the UI WGI poller. Slots retain controller identity across list
// reordering; a disconnected slot is never reported as a neutral live device.
inline void publish(unsigned index,const XINPUT_GAMEPAD* pad,Rumble rumble={}) {
    if(index>=slots.size())return;
    std::lock_guard<std::mutex> lock(mutex);
    auto& slot=slots[index];
    if(!pad){slot=Slot{};return;}
    slot.hardware=*pad;
    XINPUT_GAMEPAD visible=enabled && active?*pad:XINPUT_GAMEPAD{};
    if(!slot.connected){slot.connected=true;slot.state.dwPacketNumber=1;}
    else if(!equal(slot.state.Gamepad,visible))++slot.state.dwPacketNumber;
    slot.state.Gamepad=visible;slot.rumble=std::move(rumble);
}
inline DWORD get_state_locked(DWORD index,XINPUT_STATE* state) {
    if(index>=slots.size() || !state)return ERROR_BAD_ARGUMENTS;
    std::lock_guard<std::mutex> lock(mutex);
    if(!slots[index].connected)return ERROR_DEVICE_NOT_CONNECTED;
    *state=slots[index].state;
    state->Gamepad.wButtons&=~WORD(0x0400); // Guide is not exposed by GetState.
    return ERROR_SUCCESS;
}
inline DWORD WINAPI get_state(DWORD index,XINPUT_STATE* state) {
    DWORD result=get_state_locked(index,state);
    trace(false,index,result); // after the slot lock: note() may log
    return result;
}
inline DWORD set_state_locked(DWORD index,XINPUT_VIBRATION* vibration) {
    if(index>=slots.size() || !vibration)return ERROR_BAD_ARGUMENTS;
    std::lock_guard<std::mutex> lock(mutex);
    auto& slot=slots[index];
    if(!slot.connected)return ERROR_DEVICE_NOT_CONNECTED;
    if(!slot.rumble)return ERROR_NOT_SUPPORTED;
    if(!enabled || !active){slot.vibration=*vibration;return ERROR_SUCCESS;}
    DWORD result=slot.rumble(vibration->wLeftMotorSpeed,vibration->wRightMotorSpeed);
    if(!result)slot.vibration=*vibration;
    return result;
}
inline DWORD WINAPI set_state(DWORD index,XINPUT_VIBRATION* vibration) {
    DWORD result=set_state_locked(index,vibration);
    trace(true,index,result);
    return result;
}
// Called under mutex when either guest feedback policy or CoreWindow focus
// changes. Losing focus mutes feedback and input without losing guest policy.
inline void refresh_locked() {
    const bool visible=enabled && active;
    for(auto& slot:slots)if(slot.connected) {
        XINPUT_GAMEPAD pad=visible?slot.hardware:XINPUT_GAMEPAD{};
        if(!equal(slot.state.Gamepad,pad))++slot.state.dwPacketNumber;
        slot.state.Gamepad=pad;
        if(slot.rumble)slot.rumble(visible?slot.vibration.wLeftMotorSpeed:0,visible?slot.vibration.wRightMotorSpeed:0);
    }
}
inline void WINAPI enable(BOOL value) {
    std::lock_guard<std::mutex> lock(mutex);
    if(enabled==!!value)return;
    enabled=!!value;refresh_locked();
}
inline void set_active(bool value) {
    std::lock_guard<std::mutex> lock(mutex);
    if(active==value)return;
    active=value;refresh_locked();
}
inline DWORD WINAPI capabilities(DWORD index,DWORD,XINPUT_CAPABILITIES* caps) {
    if(index>=slots.size() || !caps)return ERROR_BAD_ARGUMENTS;
    std::lock_guard<std::mutex> lock(mutex);
    auto& slot=slots[index];
    if(!slot.connected)return ERROR_DEVICE_NOT_CONNECTED;
    *caps={};caps->Type=XINPUT_DEVTYPE_GAMEPAD;caps->SubType=XINPUT_DEVSUBTYPE_GAMEPAD;
    caps->Gamepad.wButtons=0xf3ff;
    caps->Gamepad.bLeftTrigger=caps->Gamepad.bRightTrigger=255;
    caps->Gamepad.sThumbLX=caps->Gamepad.sThumbLY=caps->Gamepad.sThumbRX=caps->Gamepad.sThumbRY=32767;
    if(slot.rumble){caps->Flags=XINPUT_CAPS_FFB_SUPPORTED;caps->Vibration={65535,65535};}
    return ERROR_SUCCESS;
}
inline DWORD WINAPI battery(DWORD index,BYTE,XINPUT_BATTERY_INFORMATION*) {
    if(index>=slots.size())return ERROR_BAD_ARGUMENTS;
    std::lock_guard<std::mutex> lock(mutex);
    // No battery-report backend yet. Match Wine's honest unsupported path.
    return slots[index].connected?ERROR_NOT_SUPPORTED:ERROR_DEVICE_NOT_CONNECTED;
}
// xinput1_3/1_4 publish fixed ordinals (Wine's spec files match Windows);
// xinput9_1_0 exports by name only. Ordinal imports map to the same entries.
inline const char* ordinal_name(const char* dll,WORD ordinal) {
    if(!dll || (_stricmp(dll,"xinput1_4.dll") && _stricmp(dll,"xinput1_3.dll")))return nullptr;
    switch(ordinal) {
    case 2: return "XInputGetState";
    case 3: return "XInputSetState";
    case 4: return "XInputGetCapabilities";
    case 5: return "XInputEnable";
    case 7: return "XInputGetBatteryInformation";
    default: return nullptr;
    }
}
// Every name under which a guest can reach the XInput API. Play serves all of
// them from the emulation: the console's own XInput never sees the pads.
inline bool handles(const char* dll) {
    return dll && (!_stricmp(dll,"xinput1_4.dll") || !_stricmp(dll,"xinput1_3.dll") ||
        !_stricmp(dll,"xinput9_1_0.dll"));
}
inline uintptr_t resolve(const char* dll,const char* name,WORD ordinal=0) {
    if(!name)name=ordinal_name(dll,ordinal);
    if(!handles(dll) || !name)return 0;
    if(!strcmp(name,"XInputGetState"))return reinterpret_cast<uintptr_t>(&get_state);
    if(!strcmp(name,"XInputSetState"))return reinterpret_cast<uintptr_t>(&set_state);
    if(!strcmp(name,"XInputGetCapabilities"))return reinterpret_cast<uintptr_t>(&capabilities);
    if(_stricmp(dll,"xinput9_1_0.dll")) {
        if(!strcmp(name,"XInputEnable"))return reinterpret_cast<uintptr_t>(&enable);
        if(!strcmp(name,"XInputGetBatteryInformation"))return reinterpret_cast<uintptr_t>(&battery);
    }
    return 0;
}
}
