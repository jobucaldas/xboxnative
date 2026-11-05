#pragma once
// Native x64 audio exports only. No success stubs or fabricated device counters.
namespace audio_routes {
static const char* const wave_symbols[]={
    "timeBeginPeriod","timeEndPeriod","timeGetTime",
    "waveInAddBuffer","waveInClose","waveInGetDevCapsA","waveInGetDevCapsW",
    "waveInGetNumDevs","waveInOpen","waveInPrepareHeader","waveInReset",
    "waveInStart","waveInUnprepareHeader","waveOutClose","waveOutGetDevCapsA",
    "waveOutGetDevCapsW","waveOutGetNumDevs","waveOutGetPosition","waveOutOpen",
    "waveOutPrepareHeader","waveOutReset","waveOutUnprepareHeader","waveOutWrite"
};
static const char* const com_symbols[]={
    "CoCreateFreeThreadedMarshaler","CoGetApartmentType","CoInitializeEx",
    "CoTaskMemAlloc","CoTaskMemFree","CoUninitialize","CoCreateGuid",
    "CoCreateInstance","CoInitialize","CoSetProxyBlanket","PropVariantClear",
    "PropVariantCopy","RoGetAgileReference","StringFromGUID2"
};
inline uintptr_t resolve(const char* dll,const char* name) {
    if(!name||_stricmp(dll,"winmm.dll"))return 0;
    for(auto symbol:wave_symbols)if(!strcmp(symbol,name)) {
        // Retain the module while mapped IATs contain its addresses.
        static HMODULE module=LoadLibraryExW(L"winmm.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        return module?reinterpret_cast<uintptr_t>(GetProcAddress(module,name)):0;
    }
    return 0;
}
inline void discover(void (*log)(const char*)) {
    unsigned missing=0;
    for(auto name:wave_symbols) {
        bool found=!!resolve("winmm.dll",name);missing+=!found;
        char line[160];snprintf(line,sizeof line,"AUD ROUTE winmm.dll!%s native=%u",name,unsigned(found));log(line);
    }
    char line[160];snprintf(line,sizeof line,"AUD ROUTES winmm total=23 resolved=%u missing=%u",23-missing,missing);log(line);
    unsigned com_missing=0;
    static HMODULE ole=LoadLibraryExW(L"ole32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    for(auto name:com_symbols) {
        bool found=ole&&GetProcAddress(ole,name);com_missing+=!found;
        snprintf(line,sizeof line,"AUD ROUTE ole32.dll!%s native=%u",name,unsigned(found));log(line);
    }
    snprintf(line,sizeof line,"AUD IMPORTS total=37 resolved=%u missing=%u",37-missing-com_missing,missing+com_missing);log(line);
}
}
