#pragma once
// Runs only the packaged native diagnostic PE. This never enters game code.
namespace audio_probe {
inline void run(void (*log)(const char*)) {
    HMODULE module=LoadPackagedLibrary(L"audio-tone.dll",0);
    if(!module) { char line[128];snprintf(line,sizeof line,"AUD FAIL packaged tone PE load error=%lu",GetLastError());log(line);return; }
    auto test=reinterpret_cast<int(WINAPI*)(void (*)(const char*))>(GetProcAddress(module,"aud_test"));
    if(test) test(log);else log("AUD FAIL aud_test export absent");
    FreeLibrary(module);
}
}
