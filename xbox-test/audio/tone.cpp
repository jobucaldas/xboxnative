// Native x64 PE diagnostic. No Wine audio, fake device, or synthetic counters.
#include <windows.h>
#include <xaudio2.h>
#include <stdint.h>
#include <stdarg.h>
// Bounded diagnostic formatter; avoid a desktop/UCRT stdio dependency.
static void aud_format(char* out, size_t cap, const char* pattern, ...) {
    va_list args;va_start(args,pattern);size_t used=0;
    auto put=[&](char c){if(used+1<cap)out[used++]=c;};
    while(*pattern) {
        if(*pattern!='%'){put(*pattern++);continue;}
        ++pattern;while(*pattern=='0'||(*pattern>='1'&&*pattern<='9'))++pattern;
        unsigned longs=0;while(*pattern=='l'){++longs;++pattern;}
        char type=*pattern++;
        uint64_t value=longs==2?va_arg(args,unsigned long long):longs==1?va_arg(args,unsigned long):va_arg(args,unsigned int);
        unsigned base=type=='x'?16:10;char digits[32];unsigned count=0;
        do{digits[count++]="0123456789abcdef"[value%base];value/=base;}while(value);
        while(count)put(digits[--count]);
    }
    if(cap)out[used]=0;va_end(args);
}
using Log = void (*)(const char*);
extern "C" __declspec(dllexport) int WINAPI aud_test(Log log) {
    // The Play worker is distinct from the UI apartment. Initialize this thread.
    HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(apartment)) { log("AUD FAIL CoInitializeEx MTA");return 1; }
    HMODULE module=LoadLibraryExW(L"xaudio2_9.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module) module=LoadLibraryExW(L"xaudio2_8.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto create=module ? reinterpret_cast<HRESULT(WINAPI*)(IXAudio2**,UINT32,XAUDIO2_PROCESSOR)>(GetProcAddress(module,"XAudio2Create")) : nullptr;
    char line[256];
    if(!create) { aud_format(line,sizeof line,"AUD FAIL XAudio2Create lookup error=%lu",GetLastError());log(line);if(module)FreeLibrary(module);CoUninitialize();return 1; }
    // Report native WinMM device availability separately from XAudio2 playback.
    HMODULE winmm=LoadLibraryExW(L"winmm.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto devices=winmm ? reinterpret_cast<UINT(WINAPI*)()>(GetProcAddress(winmm,"waveOutGetNumDevs")) : nullptr;
    aud_format(line,sizeof line,"AUD WINMM loaded=%d devices=%u",!!winmm,devices?devices():0);log(line);
    if(winmm)FreeLibrary(winmm);
    IXAudio2* engine=nullptr; IXAudio2MasteringVoice* master=nullptr; IXAudio2SourceVoice* source=nullptr;
    HRESULT hr=create(&engine,0,XAUDIO2_DEFAULT_PROCESSOR);
    aud_format(line,sizeof line,"AUD XAudio2Create hr=0x%08lx",(unsigned long)hr);log(line);
    if(SUCCEEDED(hr)) hr=engine->CreateMasteringVoice(&master,2,48000,0,nullptr,nullptr,AudioCategory_GameEffects);
    aud_format(line,sizeof line,"AUD CreateMasteringVoice hr=0x%08lx",(unsigned long)hr);log(line);
    WAVEFORMATEX format={WAVE_FORMAT_PCM,2,48000,192000,4,16,0};
    if(SUCCEEDED(hr)) hr=engine->CreateSourceVoice(&source,&format);
    // 440 Hz sine recurrence; static storage remains valid until DestroyVoice.
    static int16_t samples[48000]; unsigned nonzero=0;
    double s=0,c=1;
    for(unsigned i=0;i<24000;i++) {
        int16_t v=(int16_t)(s*4096);samples[i*2]=samples[i*2+1]=v;if(v)nonzero+=2;
        double next=s*0.9983418166140283+c*0.05756402695956728;
        c=c*0.9983418166140283-s*0.05756402695956728;s=next;
    }
    XAUDIO2_BUFFER buffer={};buffer.Flags=0;buffer.AudioBytes=sizeof samples;buffer.pAudioData=(const BYTE*)samples;
    if(SUCCEEDED(hr)) hr=source->SubmitSourceBuffer(&buffer);
    aud_format(line,sizeof line,"AUD SubmitSourceBuffer hr=0x%08lx nonzero=%u bytes=%u",(unsigned long)hr,nonzero,buffer.AudioBytes);log(line);
    if(SUCCEEDED(hr)) hr=source->Start();
    XAUDIO2_VOICE_STATE state={};
    if(SUCCEEDED(hr)) {
        for(unsigned i=0;i<100;i++) { source->GetState(&state,0);if(!state.BuffersQueued&&state.SamplesPlayed)break;Sleep(20); }
    }
    aud_format(line,sizeof line,"AUD RESULT hr=0x%08lx SamplesPlayed=%llu nonzero=%u queued=%u pass=%d",(unsigned long)hr,(unsigned long long)state.SamplesPlayed,nonzero,state.BuffersQueued,SUCCEEDED(hr)&&state.SamplesPlayed>0&&nonzero>0);log(line);
    if(source)source->DestroyVoice();if(master)master->DestroyVoice();if(engine)engine->Release();FreeLibrary(module);CoUninitialize();
    return SUCCEEDED(hr)&&state.SamplesPlayed>0&&nonzero>0 ? 0 : 1;
}
