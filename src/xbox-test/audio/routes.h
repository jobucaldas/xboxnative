#pragma once
#include <string>
#include "mm_timer.h"
// Native x64 audio exports, plus PlaySoundA as the ANSI conversion to a native
// PlaySoundW. No success stubs or fabricated device counters.
namespace audio_routes {
static const char* const wave_symbols[]={
    "timeBeginPeriod","timeEndPeriod","timeGetTime",
    "waveInAddBuffer","waveInClose","waveInGetDevCapsA","waveInGetDevCapsW",
    "waveInGetNumDevs","waveInOpen","waveInPrepareHeader","waveInReset",
    "waveInStart","waveInUnprepareHeader","waveOutClose","waveOutGetDevCapsA",
    "waveOutGetDevCapsW","waveOutGetNumDevs","waveOutGetPosition","waveOutOpen",
    "waveOutPrepareHeader","waveOutReset","waveOutUnprepareHeader","waveOutWrite",
    // gbe_fork (Steam API provider for owned games) imports these as well.
    "timeGetDevCaps","waveInGetErrorTextW","waveInMessage","waveOutGetErrorTextW",
    "waveOutMessage","waveOutPause","waveOutRestart",
    // gbe_fork's emulated Steam client (SteamStub games) plays notification sounds.
    "PlaySoundA","PlaySoundW",
    // Halo Wars: Definitive Edition sets its wave output volume.
    "waveOutSetVolume",
    // Mono (Unity) uses multimedia timers.
    "timeSetEvent","timeKillEvent"
};
constexpr unsigned wave_count=sizeof(wave_symbols)/sizeof(wave_symbols[0]);
static const char* const com_symbols[]={
    "CoCreateFreeThreadedMarshaler","CoGetApartmentType","CoInitializeEx",
    "CoTaskMemAlloc","CoTaskMemFree","CoUninitialize","CoCreateGuid",
    "CoCreateInstance","CoInitialize","CoSetProxyBlanket","PropVariantClear",
    "PropVariantCopy","RoGetAgileReference","StringFromGUID2"
};
inline uintptr_t native(const char* name) {
    // Retain the module while mapped IATs contain its addresses.
    static HMODULE module=LoadLibraryExW(L"winmm.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module?reinterpret_cast<uintptr_t>(GetProcAddress(module,name)):0;
}
// PlaySoundA as Windows implements it: a sound name or alias is converted to
// UTF-16 for PlaySoundW; memory images and integer resource IDs pass through.
// Used only where the console's winmm exports PlaySoundW alone.
inline BOOL WINAPI play_sound_a(LPCSTR sound,HMODULE module,DWORD flags) {
    using Wide=BOOL(WINAPI*)(LPCWSTR,HMODULE,DWORD);
    static auto wide=reinterpret_cast<Wide>(native("PlaySoundW"));
    if(!wide){SetLastError(ERROR_PROC_NOT_FOUND);return FALSE;}
    const bool memory=(flags&0x00040004/*SND_RESOURCE*/)==0x0004/*SND_MEMORY*/;
    if(!sound||memory||IS_INTRESOURCE(sound))return wide(reinterpret_cast<LPCWSTR>(sound),module,flags);
    int n=MultiByteToWideChar(CP_ACP,0,sound,-1,nullptr,0);if(n<=0)return FALSE;
    std::wstring name(size_t(n),L'\0');MultiByteToWideChar(CP_ACP,0,sound,-1,&name[0],n);
    return wide(name.c_str(),module,flags);
}
inline uintptr_t resolve(const char* dll,const char* name) {
    if(!name||_stricmp(dll,"winmm.dll"))return 0;
    for(auto symbol:wave_symbols)if(!strcmp(symbol,name)) {
        uintptr_t p=native(name);
        if(!p&&!strcmp(name,"PlaySoundA")&&native("PlaySoundW"))p=reinterpret_cast<uintptr_t>(&play_sound_a);
        // Multimedia timers the console's winmm lacks (mm_timer.h).
        if(!p&&!strcmp(name,"timeSetEvent"))p=reinterpret_cast<uintptr_t>(&mm_timer::set_event);
        if(!p&&!strcmp(name,"timeKillEvent"))p=reinterpret_cast<uintptr_t>(&mm_timer::kill_event);
        return p;
    }
    return 0;
}
inline void discover(void (*log)(const char*)) {
    unsigned missing=0;
    for(auto name:wave_symbols) {
        bool found=!!resolve("winmm.dll",name);missing+=!found;
        bool adapted=found&&!native(name);
        char line[160];snprintf(line,sizeof line,"AUD ROUTE winmm.dll!%s native=%u%s",name,unsigned(found&&!adapted),adapted?(strcmp(name,"PlaySoundA")?" adapter=thread-pool timer":" adapter=PlaySoundW"):"");log(line);
    }
    char line[160];snprintf(line,sizeof line,"AUD ROUTES winmm total=%u resolved=%u missing=%u",wave_count,wave_count-missing,missing);log(line);
    unsigned com_missing=0;
    static HMODULE ole=LoadLibraryExW(L"ole32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    for(auto name:com_symbols) {
        bool found=ole&&GetProcAddress(ole,name);com_missing+=!found;
        snprintf(line,sizeof line,"AUD ROUTE ole32.dll!%s native=%u",name,unsigned(found));log(line);
    }
    snprintf(line,sizeof line,"AUD IMPORTS total=37 resolved=%u missing=%u",37-missing-com_missing,missing+com_missing);log(line);
}
}
