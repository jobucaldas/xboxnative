#pragma once
// Runs only the packaged native diagnostic PE. This never enters game code.
namespace audio_probe {
inline int run(void (*log)(const char*)) {
    HMODULE module=LoadPackagedLibrary(L"audio-tone.dll",0);
    if(!module) { char line[128];snprintf(line,sizeof line,"AUD FAIL packaged tone PE load error=%lu",GetLastError());log(line);return 1; }
    auto test=reinterpret_cast<int(WINAPI*)(void (*)(const char*))>(GetProcAddress(module,"aud_test"));
    int result=test ? test(log) : 1;
    if(!test) log("AUD FAIL aud_test export absent");
    FreeLibrary(module);
    return result;
}
}
