// Native x64 PE diagnostic. No Wine audio, fake device, or synthetic counters.
#include <windows.h>
#include <xaudio2.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <new>
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
using HSTRING = void*;
using Guid = GUID;
static const Guid iidMediaDeviceStatics={0xaa2d9a40,0x909f,0x4bba,{0xbf,0x8b,0x0c,0x0d,0x29,0x6f,0x14,0xf0}};
static const Guid iidAudioClient={0x1cb9ad4c,0xdbfa,0x4c32,{0xb1,0x78,0xc2,0xf5,0x68,0xa7,0x03,0xb2}};
static const Guid iidUnknown={0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const Guid iidActivationHandler={0x41d949ab,0x9862,0x444a,{0x80,0xf6,0xc2,0x61,0x33,0x4d,0xa5,0xeb}};
static const Guid clsidMmDeviceEnumerator={0xbcde0395,0xe52f,0x467c,{0x8e,0x3d,0xc4,0x57,0x92,0x91,0x69,0x2e}};
static const Guid iidMmDeviceEnumerator={0xa95664d2,0x9614,0x4f35,{0xa7,0x46,0xde,0x8d,0xb6,0x36,0x17,0xe6}};
using RoInitializeFn = HRESULT (WINAPI*)(UINT);
using RoUninitializeFn = void (WINAPI*)();
using WindowsCreateStringFn = HRESULT (WINAPI*)(PCNZWCH,UINT32,HSTRING*);
using WindowsDeleteStringFn = HRESULT (WINAPI*)(HSTRING);
using RoGetActivationFactoryFn = HRESULT (WINAPI*)(HSTRING,REFIID,void**);
using WindowsGetStringRawBufferFn = PCWSTR (WINAPI*)(HSTRING,UINT32*);
using ActivateAudioInterfaceAsyncFn = HRESULT (WINAPI*)(LPCWSTR,REFIID,PROPVARIANT*,IActivateAudioInterfaceCompletionHandler*,IActivateAudioInterfaceAsyncOperation**);
static bool same_guid(REFIID left,const Guid& right) {
    const auto& value=reinterpret_cast<const Guid&>(left);
    return value.Data1==right.Data1&&value.Data2==right.Data2&&value.Data3==right.Data3&&
        !memcmp(value.Data4,right.Data4,sizeof value.Data4);
}
struct ActivationHandler : IActivateAudioInterfaceCompletionHandler {
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    volatile LONG refs=1;
    HRESULT callHr=E_FAIL,activationHr=E_PENDING;
    IUnknown* activated=nullptr;
    ~ActivationHandler() {if(activated)activated->Release();if(ready)CloseHandle(ready);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(same_guid(iid,iidUnknown)||same_guid(iid,iidActivationHandler)){*out=this;AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return (ULONG)InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining=(ULONG)InterlockedDecrement(&refs);
        if(!remaining) {this->~ActivationHandler();HeapFree(GetProcessHeap(),0,this);}
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
        callHr=operation->GetActivateResult(&activationHr,&activated);
        SetEvent(ready);return S_OK;
    }
};
static void probe_audio_endpoints(Log log, bool winrtReady) {
    HMODULE base=LoadLibraryExW(L"combase.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto createString=base?reinterpret_cast<WindowsCreateStringFn>(GetProcAddress(base,"WindowsCreateString")):nullptr;
    auto deleteString=base?reinterpret_cast<WindowsDeleteStringFn>(GetProcAddress(base,"WindowsDeleteString")):nullptr;
    auto getFactory=base?reinterpret_cast<RoGetActivationFactoryFn>(GetProcAddress(base,"RoGetActivationFactory")):nullptr;
    auto getBuffer=base?reinterpret_cast<WindowsGetStringRawBufferFn>(GetProcAddress(base,"WindowsGetStringRawBuffer")):nullptr;
    HRESULT hr=base&&createString&&deleteString&&getFactory&&getBuffer&&winrtReady?S_OK:E_NOTIMPL;
    HSTRING className=nullptr,endpoint=nullptr;void* factory=nullptr;
    if(SUCCEEDED(hr))hr=createString(L"Windows.Media.Devices.MediaDevice",34,&className);
    if(SUCCEEDED(hr))hr=getFactory(className,iidMediaDeviceStatics,&factory);
    char line[192];aud_format(line,sizeof line,"AUD STAGE MediaDevice.GetDefaultAudioRenderId factory_hr=0x%08lx",(unsigned long)hr);log(line);
    HRESULT idHr=hr;
    if(SUCCEEDED(idHr)) {
        auto table=*reinterpret_cast<void***>(factory);
        using GetDefaultAudioRenderIdFn=HRESULT (STDMETHODCALLTYPE*)(void*,int,HSTRING*);
        idHr=reinterpret_cast<GetDefaultAudioRenderIdFn>(table[10])(factory,0,&endpoint);
    }
    UINT32 endpointLength=0;PCWSTR endpointId=SUCCEEDED(idHr)&&getBuffer?getBuffer(endpoint,&endpointLength):nullptr;
    aud_format(line,sizeof line,"AUD STAGE GetDefaultAudioRenderId hr=0x%08lx endpoint=%u",(unsigned long)idHr,endpointId&&endpointLength?1u:0u);log(line);
    HMODULE mmdev=LoadLibraryExW(L"mmdevapi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto activate=mmdev?reinterpret_cast<ActivateAudioInterfaceAsyncFn>(GetProcAddress(mmdev,"ActivateAudioInterfaceAsync")):nullptr;
    HRESULT activateHr=endpointId&&endpointLength&&activate?S_OK:FAILED(idHr)?idHr:E_NOTIMPL;
    HRESULT completionHr=E_PENDING, activationHr=E_PENDING;
    void* handlerMemory=HeapAlloc(GetProcessHeap(),0,sizeof(ActivationHandler));
    ActivationHandler* handler=handlerMemory?new(handlerMemory) ActivationHandler:nullptr;
    if(SUCCEEDED(activateHr)&&handler&&handler->ready) {
        IActivateAudioInterfaceAsyncOperation* operation=nullptr;
        activateHr=activate(endpointId,iidAudioClient,nullptr,handler,&operation);
        if(SUCCEEDED(activateHr)) {
            if(WaitForSingleObject(handler->ready,5000)==WAIT_OBJECT_0) {
                completionHr=handler->callHr;activationHr=handler->activationHr;
            } else completionHr=HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        if(operation)operation->Release();
    } else if(SUCCEEDED(activateHr)) activateHr=E_OUTOFMEMORY;
    aud_format(line,sizeof line,"AUD STAGE ActivateAudioInterfaceAsync hr=0x%08lx completion_hr=0x%08lx activation_hr=0x%08lx",(unsigned long)activateHr,(unsigned long)completionHr,(unsigned long)activationHr);log(line);
    if(handler)handler->Release();
    if(endpoint&&deleteString)deleteString(endpoint);if(className&&deleteString)deleteString(className);
    if(factory)reinterpret_cast<IUnknown*>(factory)->Release();if(mmdev)FreeLibrary(mmdev);if(base)FreeLibrary(base);
    IMMDeviceEnumerator* enumerator=nullptr;
    HRESULT enumHr=CoCreateInstance(reinterpret_cast<const GUID&>(clsidMmDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,
                                    reinterpret_cast<const GUID&>(iidMmDeviceEnumerator),reinterpret_cast<void**>(&enumerator));
    aud_format(line,sizeof line,"AUD STAGE IMMDeviceEnumerator hr=0x%08lx",(unsigned long)enumHr);log(line);
    IMMDevice* device=nullptr;
    HRESULT endpointHr=SUCCEEDED(enumHr)?enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device):enumHr;
    aud_format(line,sizeof line,"AUD STAGE IMMDeviceEnumerator.GetDefaultAudioEndpoint hr=0x%08lx",(unsigned long)endpointHr);log(line);
    if(device)device->Release();if(enumerator)enumerator->Release();
}
extern "C" __declspec(dllexport) int WINAPI aud_test(Log log) {
    // The Play worker is distinct from the UI apartment. Initialize this thread
    // for WinRT first; retain the COM fallback so XAudio2 still gets diagnosed.
    HMODULE combase=LoadLibraryExW(L"combase.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto roInitialize=combase?reinterpret_cast<RoInitializeFn>(GetProcAddress(combase,"RoInitialize")):nullptr;
    auto roUninitialize=combase?reinterpret_cast<RoUninitializeFn>(GetProcAddress(combase,"RoUninitialize")):nullptr;
    HRESULT initHr=roInitialize?roInitialize(1):E_NOTIMPL;
    bool winrtReady=SUCCEEDED(initHr),comOwned=winrtReady;
    if(!winrtReady) { HRESULT fallback=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(SUCCEEDED(fallback)){initHr=fallback;comOwned=true;} }
    char line[256];aud_format(line,sizeof line,"AUD STAGE COM.Initialize hr=0x%08lx winrt=%u",(unsigned long)initHr,winrtReady?1u:0u);log(line);
    probe_audio_endpoints(log,winrtReady);
    HMODULE module=LoadLibraryExW(L"xaudio2_9.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module) module=LoadLibraryExW(L"xaudio2_8.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto create=module ? reinterpret_cast<HRESULT(WINAPI*)(IXAudio2**,UINT32,XAUDIO2_PROCESSOR)>(GetProcAddress(module,"XAudio2Create")) : nullptr;
    DWORD lookupError=create?ERROR_SUCCESS:GetLastError();
    if(!create) { if(!lookupError)lookupError=ERROR_PROC_NOT_FOUND;aud_format(line,sizeof line,"AUD FAIL XAudio2Create lookup error=%lu",lookupError);log(line); }
    // Report native WinMM device availability separately from XAudio2 playback.
    HMODULE winmm=LoadLibraryExW(L"winmm.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto devices=winmm ? reinterpret_cast<UINT(WINAPI*)()>(GetProcAddress(winmm,"waveOutGetNumDevs")) : nullptr;
    aud_format(line,sizeof line,"AUD WINMM loaded=%d devices=%u",!!winmm,devices?devices():0);log(line);
    if(winmm)FreeLibrary(winmm);
    IXAudio2* engine=nullptr; IXAudio2MasteringVoice* master=nullptr; IXAudio2SourceVoice* source=nullptr;
    HRESULT hr=create?create(&engine,0,XAUDIO2_DEFAULT_PROCESSOR):HRESULT_FROM_WIN32(lookupError);
    aud_format(line,sizeof line,"AUD STAGE XAudio2Create hr=0x%08lx",(unsigned long)hr);log(line);
    if(SUCCEEDED(hr)) hr=engine->CreateMasteringVoice(&master,2,48000,0,nullptr,nullptr,AudioCategory_GameEffects);
    aud_format(line,sizeof line,"AUD STAGE CreateMasteringVoice hr=0x%08lx",(unsigned long)hr);log(line);
    WAVEFORMATEX format={WAVE_FORMAT_PCM,2,48000,192000,4,16,0};
    if(SUCCEEDED(hr)) hr=engine->CreateSourceVoice(&source,&format);
    aud_format(line,sizeof line,"AUD STAGE CreateSourceVoice hr=0x%08lx",(unsigned long)hr);log(line);
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
    aud_format(line,sizeof line,"AUD STAGE SubmitSourceBuffer hr=0x%08lx nonzero=%u bytes=%u",(unsigned long)hr,nonzero,buffer.AudioBytes);log(line);
    if(SUCCEEDED(hr)) hr=source->Start();
    aud_format(line,sizeof line,"AUD STAGE SourceVoice.Start hr=0x%08lx",(unsigned long)hr);log(line);
    XAUDIO2_VOICE_STATE state={};
    if(SUCCEEDED(hr)) {
        for(unsigned i=0;i<100;i++) { source->GetState(&state,0);if(!state.BuffersQueued&&state.SamplesPlayed)break;Sleep(20); }
    }
    aud_format(line,sizeof line,"AUD STAGE SamplesPlayed hr=0x%08lx SamplesPlayed=%llu queued=%u",(unsigned long)hr,(unsigned long long)state.SamplesPlayed,state.BuffersQueued);log(line);
    aud_format(line,sizeof line,"AUD RESULT hr=0x%08lx SamplesPlayed=%llu nonzero=%u queued=%u pass=%d",(unsigned long)hr,(unsigned long long)state.SamplesPlayed,nonzero,state.BuffersQueued,state.SamplesPlayed>0);log(line);
    if(source)source->DestroyVoice();if(master)master->DestroyVoice();if(engine)engine->Release();if(module)FreeLibrary(module);
    if(winrtReady&&roUninitialize)roUninitialize();else if(comOwned)CoUninitialize();if(combase)FreeLibrary(combase);
    return state.SamplesPlayed>0 ? 0 : 1;
}
