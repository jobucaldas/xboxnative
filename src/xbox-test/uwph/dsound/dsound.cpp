// SPDX-License-Identifier: GPL-3.0-or-later
// DirectSound 8 compatibility surface backed by the console's XAudio2 engine.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <initguid.h>
#include <dsound.h>
#include <xaudio2.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <new>
#include "ring.h"

extern "C" void *memcpy(void *to,const void *from,size_t count){auto *d=static_cast<unsigned char*>(to);auto *s=static_cast<const unsigned char*>(from);for(size_t i=0;i<count;++i)d[i]=s[i];return to;}
extern "C" void *memset(void *to,int value,size_t count){auto *d=static_cast<unsigned char*>(to);for(size_t i=0;i<count;++i)d[i]=static_cast<unsigned char>(value);return to;}
extern "C" int memcmp(const void *left,const void *right,size_t count){auto *a=static_cast<const unsigned char*>(left);auto *b=static_cast<const unsigned char*>(right);for(size_t i=0;i<count;++i)if(a[i]!=b[i])return int(a[i])-int(b[i]);return 0;}

namespace {
using CreateEngine = HRESULT (WINAPI *)(IXAudio2 **, UINT32, XAUDIO2_PROCESSOR);
static HMODULE engine_module;
static IXAudio2 *engine;
static IXAudio2MasteringVoice *master;
static HRESULT engine_result = E_PENDING;
static WAVEFORMATEX primary_format = {WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
static GUID default_playback_id = {0xDEF00000,0x9C6D,0x47ED,{0xAA,0xF1,0x4D,0xDA,0x8F,0x2B,0x5C,0x03}};

static HRESULT ensure_engine() {
    if (engine_result != E_PENDING) return engine_result;
    engine_module = LoadLibraryExW(L"xaudio2_9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!engine_module) engine_module = LoadLibraryExW(L"xaudio2_8.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto create = engine_module ? reinterpret_cast<CreateEngine>(GetProcAddress(engine_module, "XAudio2Create")) : nullptr;
    engine_result = create ? create(&engine, 0, XAUDIO2_DEFAULT_PROCESSOR) : DSERR_NODRIVER;
    if (SUCCEEDED(engine_result)) engine_result = engine->CreateMasteringVoice(&master, 2, 48000, 0, nullptr, nullptr, AudioCategory_GameEffects);
    if (FAILED(engine_result)) {
        if (engine) { engine->Release(); engine = nullptr; }
        if (engine_module) { FreeLibrary(engine_module); engine_module = nullptr; }
        engine_result = DSERR_NODRIVER;
    }
    return engine_result;
}

static float volume_scalar(LONG millibels) {
    if(millibels<=DSBVOLUME_MIN)return 0.0f;
    float value=1.0f;
    while(millibels<=-1000){value*=0.316227766f;millibels+=1000;}
    while(millibels<=-100){value*=0.891250938f;millibels+=100;}
    while(millibels<0){value*=0.988553095f;millibels+=10;}
    return value;
}

static bool valid_format(const WAVEFORMATEX *f) {
    return f && f->wFormatTag == WAVE_FORMAT_PCM && f->nChannels >= 1 && f->nChannels <= 2 &&
           (f->wBitsPerSample == 8 || f->wBitsPerSample == 16) && f->nBlockAlign == f->nChannels * (f->wBitsPerSample / 8) &&
           f->nSamplesPerSec && f->nAvgBytesPerSec == f->nSamplesPerSec * f->nBlockAlign;
}

class Buffer;
class VoiceEvents final : public IXAudio2VoiceCallback {
public:
    explicit VoiceEvents(Buffer *owner) : owner_(owner) {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override;
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void *) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void *) override;
    void STDMETHODCALLTYPE OnLoopEnd(void *) override;
    void STDMETHODCALLTYPE OnVoiceError(void *, HRESULT) override;
private:
    Buffer *owner_;
};

class Buffer final : public IDirectSoundBuffer8, public IDirectSoundNotify {
public:
    Buffer(bool primary, DWORD flags, DWORD bytes, const WAVEFORMATEX *format)
        : primary_(primary), flags_(flags), byte_count_(bytes), refs_(1), play_cursor_(0), frequency_(format->nSamplesPerSec),
          volume_(0), pan_(0), looping_(false), playing_(false), locked_(false), lock_offset_(0), lock_bytes_(0), voice_(nullptr),
          voice_callback_(this), start_cursor_(0), start_samples_(0), last_notify_samples_(0), notification_count_(0) {
        format_ = *format;
        block_align_ = format_.nBlockAlign;
        if (!primary_ && byte_count_) data_ = static_cast<uint8_t *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, byte_count_));
    }
    ~Buffer() {
        stop_voice();
        if (data_) HeapFree(GetProcessHeap(), 0, data_);
    }
    bool valid() const { return primary_ || data_; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (IsEqualIID(iid,IID_IUnknown)||IsEqualIID(iid,IID_IDirectSoundBuffer)||IsEqualIID(iid,IID_IDirectSoundBuffer8)) *out=static_cast<IDirectSoundBuffer8*>(this);
        else if (IsEqualIID(iid,IID_IDirectSoundNotify)) *out=static_cast<IDirectSoundNotify*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n=(ULONG)InterlockedDecrement(&refs_); if(!n){this->~Buffer();HeapFree(GetProcessHeap(),0,this);}return n; }
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS caps) override {
        if(!caps||caps->dwSize<sizeof(DSBCAPS))return DSERR_INVALIDPARAM;
        DWORD size=caps->dwSize; memset(caps,0,sizeof(*caps));caps->dwSize=size;caps->dwFlags=flags_;caps->dwBufferBytes=byte_count_;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD play,LPDWORD write) override {
        if(primary_) {if(play)*play=0;if(write)*write=0;return S_OK;}
        uint32_t p=current_cursor(); if(play)*play=p;
        if(write){uint32_t lead=frequency_*block_align_/50;uint32_t w=(p+lead)%byte_count_;*write=w-w%block_align_;}
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFormat(LPWAVEFORMATEX out,DWORD cap,LPDWORD written) override {
        DWORD size=sizeof(WAVEFORMATEX);if(written)*written=size;
        if(!out)return S_OK;if(cap<size)return DSERR_INVALIDPARAM;memcpy(out,&format_,size);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVolume(LPLONG out) override {if(!out)return DSERR_INVALIDPARAM;*out=volume_;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPan(LPLONG out) override {if(!out)return DSERR_INVALIDPARAM;*out=pan_;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD out) override {if(!out)return DSERR_INVALIDPARAM;*out=frequency_;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD out) override {if(!out)return DSERR_INVALIDPARAM;*out=playing_?DSBSTATUS_PLAYING|(looping_?DSBSTATUS_LOOPING:0):0;return S_OK;}
    HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTSOUND,LPCDSBUFFERDESC) override {return DSERR_ALREADYINITIALIZED;}
    HRESULT STDMETHODCALLTYPE Lock(DWORD offset,DWORD bytes,LPVOID *p1,LPDWORD n1,LPVOID *p2,LPDWORD n2,DWORD flags) override {
        if(!p1||!n1||!p2||!n2||primary_||!data_)return DSERR_INVALIDPARAM;
        *p1=*p2=nullptr;*n1=*n2=0;
        if(flags&DSBLOCK_FROMWRITECURSOR) offset=write_cursor();
        dsound_core::Regions regions{};dsound_core::Ring ring(data_,byte_count_,block_align_);
        if(!ring.lock(offset,bytes,(flags&DSBLOCK_ENTIREBUFFER)!=0,regions))return DSERR_INVALIDCALL;
        locked_=true;lock_offset_=ring.lock_offset();lock_bytes_=ring.lock_bytes();
        *p1=data_+lock_offset_;*n1=regions.first_bytes;if(regions.second_bytes){*p2=data_;*n2=regions.second_bytes;}return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Play(DWORD,DWORD,DWORD flags) override {
        if(primary_)return DSERR_INVALIDCALL;
        if(FAILED(ensure_engine()))return DSERR_NODRIVER;
        if(!voice_){HRESULT hr=engine->CreateSourceVoice(&voice_,&format_,0,XAUDIO2_DEFAULT_FREQ_RATIO,&voice_callback_);if(FAILED(hr))return hr;apply_controls();}
        if(playing_)voice_->Stop(0);
        voice_->FlushSourceBuffers();
        XAUDIO2_BUFFER b{};b.AudioBytes=byte_count_;b.pAudioData=data_;b.PlayBegin=play_cursor_/block_align_;b.PlayLength=byte_count_/block_align_-b.PlayBegin;
        looping_=(flags&DSBPLAY_LOOPING)!=0;if(looping_){b.LoopBegin=0;b.LoopLength=byte_count_/block_align_;b.LoopCount=XAUDIO2_LOOP_INFINITE;}
        HRESULT hr=voice_->SubmitSourceBuffer(&b);if(FAILED(hr))return hr;
        XAUDIO2_VOICE_STATE state{};voice_->GetState(&state,0);start_samples_=state.SamplesPlayed;
        start_cursor_=play_cursor_;last_notify_samples_=0;playing_=true;
        hr=voice_->Start(0);if(FAILED(hr))playing_=false;return hr;
    }
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD position) override {
        if(primary_||!byte_count_||position>=byte_count_||position%block_align_)return DSERR_INVALIDPARAM;
        bool restart=playing_;if(restart){voice_->Stop(0);voice_->FlushSourceBuffers();playing_=false;}
        play_cursor_=position;if(restart)return Play(0,0,looping_?DSBPLAY_LOOPING:0);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX format) override {
        if(!primary_||!valid_format(format))return DSERR_BADFORMAT;primary_format=*format;format_=*format;block_align_=format_.nBlockAlign;frequency_=format_.nSamplesPerSec;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVolume(LONG value) override {
        if(!(flags_&DSBCAPS_CTRLVOLUME))return DSERR_CONTROLUNAVAIL;if(value<DSBVOLUME_MIN||value>DSBVOLUME_MAX)return DSERR_INVALIDPARAM;
        volume_=value;if(voice_)return voice_->SetVolume(volume_scalar(value),0);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPan(LONG value) override {
        if(!(flags_&DSBCAPS_CTRLPAN))return DSERR_CONTROLUNAVAIL;if(value<DSBPAN_LEFT||value>DSBPAN_RIGHT)return DSERR_INVALIDPARAM;
        pan_=value;if(voice_)apply_pan();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFrequency(DWORD value) override {
        if(!(flags_&DSBCAPS_CTRLFREQUENCY))return DSERR_CONTROLUNAVAIL;if(value!=DSBFREQUENCY_ORIGINAL&&(value<DSBFREQUENCY_MIN||value>DSBFREQUENCY_MAX))return DSERR_INVALIDPARAM;
        frequency_=value==DSBFREQUENCY_ORIGINAL?format_.nSamplesPerSec:value;return voice_?voice_->SetFrequencyRatio(float(frequency_)/format_.nSamplesPerSec,0):S_OK;
    }
    HRESULT STDMETHODCALLTYPE Stop() override {if(voice_&&playing_){play_cursor_=current_cursor();voice_->Stop(0);}playing_=false;signal_stop();return S_OK;}
    HRESULT STDMETHODCALLTYPE Unlock(LPVOID p1,DWORD n1,LPVOID p2,DWORD n2) override {
        if(!locked_||p1!=data_+lock_offset_||n1>lock_bytes_||((n2!=0)!=(p2==data_))||n1+n2!=lock_bytes_)return DSERR_INVALIDPARAM;
        DWORD expected_first=byte_count_-lock_offset_;if(expected_first>lock_bytes_)expected_first=lock_bytes_;
        if(n1!=expected_first||n2!=lock_bytes_-expected_first)return DSERR_INVALIDPARAM;
        locked_=false;lock_bytes_=0;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Restore() override {return S_OK;}
    HRESULT STDMETHODCALLTYPE SetFX(DWORD,LPDSEFFECTDESC,LPDWORD) override {return DSERR_FXUNAVAILABLE;}
    HRESULT STDMETHODCALLTYPE AcquireResources(DWORD,DWORD,LPDWORD) override {return DSERR_UNSUPPORTED;}
    HRESULT STDMETHODCALLTYPE GetObjectInPath(REFGUID,DWORD,REFGUID,LPVOID *out) override {if(out)*out=nullptr;return DSERR_OBJECTNOTFOUND;}
    HRESULT STDMETHODCALLTYPE SetNotificationPositions(DWORD count,LPCDSBPOSITIONNOTIFY positions) override {
        if(count>64||(!positions&&count))return DSERR_INVALIDPARAM;
        for(DWORD i=0;i<count;++i)if(!positions[i].hEventNotify||(positions[i].dwOffset!=DSBPN_OFFSETSTOP&&positions[i].dwOffset>=byte_count_))return DSERR_INVALIDPARAM;
        notification_count_=0;
        // Event handles belong to the caller; this in-process object only signals them.
        for(DWORD i=0;i<count;++i)if(positions[i].hEventNotify)notifications_[notification_count_++]={positions[i].dwOffset,positions[i].hEventNotify};
        return S_OK;
    }
    uint32_t current_cursor() {
        if(!voice_||!playing_)return play_cursor_;
        XAUDIO2_VOICE_STATE state{};voice_->GetState(&state,0);
        uint64_t played=state.SamplesPlayed>=start_samples_?state.SamplesPlayed-start_samples_:0;
        uint64_t position=uint64_t(start_cursor_)+played*block_align_;
        if(looping_)position%=byte_count_;else if(position>=byte_count_){play_cursor_=byte_count_-block_align_;playing_=false;return play_cursor_;}
        return uint32_t(position-position%block_align_);
    }
    uint32_t write_cursor(){uint32_t p=current_cursor();uint32_t lead=frequency_*block_align_/50;uint32_t w=byte_count_?(p+lead)%byte_count_:0;return w-w%block_align_;}
    void callback_pass() {
        if(!playing_||!byte_count_)return;
        XAUDIO2_VOICE_STATE state{};voice_->GetState(&state,0);
        uint64_t played=state.SamplesPlayed>=start_samples_?state.SamplesPlayed-start_samples_:0;
        dsound_core::Ring ring(data_,byte_count_,block_align_);
        for(DWORD i=0;i<notification_count_;++i){DWORD target=notifications_[i].offset;
            if(target!=DSBPN_OFFSETSTOP&&ring.crossed(last_notify_samples_,played,start_cursor_,target,looping_))SetEvent(notifications_[i].event);
        }
        last_notify_samples_=played;
    }
    void callback_end(){playing_=false;signal_stop();}
    void callback_error(){playing_=false;}
    bool is_primary()const{return primary_;}
    DWORD bytes()const{return byte_count_;}
    void copy_from(const Buffer& other){if(!primary_&&data_&&other.data_&&byte_count_==other.byte_count_)memcpy(data_,other.data_,byte_count_);}
private:
    void stop_voice(){if(voice_){voice_->Stop(0);voice_->FlushSourceBuffers();voice_->DestroyVoice();voice_=nullptr;}}
    void apply_controls(){if(voice_){voice_->SetVolume(volume_scalar(volume_),0);apply_pan();voice_->SetFrequencyRatio(float(frequency_)/format_.nSamplesPerSec,0);}}
    void apply_pan(){if(!voice_)return;float left=pan_>0?1.0f-float(pan_)/DSBPAN_RIGHT:1.0f;float right=pan_<0?1.0f+float(pan_)/-DSBPAN_LEFT:1.0f;if(format_.nChannels==1){float matrix[2]={left,right};voice_->SetOutputMatrix(master,1,2,matrix,0);}else{float matrix[4]={left,0.0f,0.0f,right};voice_->SetOutputMatrix(master,2,2,matrix,0);}}
    void signal_stop(){for(DWORD i=0;i<notification_count_;++i)if(notifications_[i].offset==DSBPN_OFFSETSTOP)SetEvent(notifications_[i].event);}
    bool primary_;DWORD flags_,byte_count_;LONG refs_;WAVEFORMATEX format_{};DWORD block_align_;uint8_t *data_=nullptr;
    DWORD play_cursor_,frequency_;LONG volume_,pan_;bool looping_,playing_,locked_;DWORD lock_offset_,lock_bytes_;
    IXAudio2SourceVoice *voice_;VoiceEvents voice_callback_;DWORD start_cursor_;uint64_t start_samples_,last_notify_samples_;
    struct Note{DWORD offset;HANDLE event;};Note notifications_[64]{};DWORD notification_count_;
};

void VoiceEvents::OnVoiceProcessingPassStart(UINT32){owner_->callback_pass();}
void VoiceEvents::OnBufferEnd(void*){owner_->callback_end();}
void VoiceEvents::OnLoopEnd(void*){owner_->callback_pass();}
void VoiceEvents::OnVoiceError(void*,HRESULT){owner_->callback_error();}

class Device final : public IDirectSound8 {
public:
    Device():refs_(1){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **out)override{if(!out)return E_POINTER;*out=nullptr;if(IsEqualIID(iid,IID_IUnknown)||IsEqualIID(iid,IID_IDirectSound)||IsEqualIID(iid,IID_IDirectSound8)){*out=this;AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return (ULONG)InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release()override{ULONG n=(ULONG)InterlockedDecrement(&refs_);if(!n){this->~Device();HeapFree(GetProcessHeap(),0,this);}return n;}
    HRESULT STDMETHODCALLTYPE CreateSoundBuffer(LPCDSBUFFERDESC desc,LPLPDIRECTSOUNDBUFFER out,IUnknown *outer)override{
        if(!out)return E_POINTER;*out=nullptr;if(outer)return DSERR_NOAGGREGATION;if(!desc||desc->dwSize<sizeof(DSBUFFERDESC))return DSERR_INVALIDPARAM;
        HRESULT hr=ensure_engine();if(FAILED(hr))return DSERR_NODRIVER;
        bool primary=(desc->dwFlags&DSBCAPS_PRIMARYBUFFER)!=0;const WAVEFORMATEX *format=primary?&primary_format:desc->lpwfxFormat;
        if(!format)format=&primary_format;if(!valid_format(format))return DSERR_BADFORMAT;
        DWORD bytes=primary?0:desc->dwBufferBytes;if(!primary&&(!bytes||bytes>16u*1024u*1024u||bytes%format->nBlockAlign))return DSERR_INVALIDPARAM;
        if(desc->dwFlags&DSBCAPS_CTRL3D)return E_NOINTERFACE;
        void *mem=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Buffer));if(!mem)return E_OUTOFMEMORY;
        auto *buffer=new(mem) Buffer(primary,desc->dwFlags,bytes,format);if(!buffer->valid()){buffer->~Buffer();HeapFree(GetProcessHeap(),0,mem);return E_OUTOFMEMORY;}
        *out=reinterpret_cast<IDirectSoundBuffer*>(static_cast<IDirectSoundBuffer8*>(buffer));return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS caps)override{if(!caps||caps->dwSize<sizeof(DSCAPS))return DSERR_INVALIDPARAM;DWORD size=caps->dwSize;memset(caps,0,sizeof(*caps));caps->dwSize=size;caps->dwFlags=DSCAPS_PRIMARYSTEREO|DSCAPS_PRIMARY16BIT|DSCAPS_SECONDARYMONO|DSCAPS_SECONDARYSTEREO|DSCAPS_SECONDARY8BIT|DSCAPS_SECONDARY16BIT;caps->dwMinSecondarySampleRate=100;caps->dwMaxSecondarySampleRate=200000;return S_OK;}
    HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER original,LPLPDIRECTSOUNDBUFFER out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!original)return DSERR_INVALIDPARAM;IDirectSoundBuffer8 *source=nullptr;HRESULT hr=original->QueryInterface(IID_IDirectSoundBuffer8,reinterpret_cast<void**>(&source));if(FAILED(hr))return hr;
        DSBCAPS caps{};caps.dwSize=sizeof(caps);hr=source->GetCaps(&caps);WAVEFORMATEX fmt{};DWORD size=0;if(SUCCEEDED(hr))hr=source->GetFormat(&fmt,sizeof(fmt),&size);
        if(SUCCEEDED(hr)){DSBUFFERDESC desc{};desc.dwSize=sizeof(desc);desc.dwFlags=caps.dwFlags;desc.dwBufferBytes=caps.dwBufferBytes;desc.lpwfxFormat=&fmt;hr=CreateSoundBuffer(&desc,out,nullptr);if(SUCCEEDED(hr)&&caps.dwBufferBytes){IDirectSoundBuffer8 *dest=nullptr;hr=(*out)->QueryInterface(IID_IDirectSoundBuffer8,reinterpret_cast<void**>(&dest));if(SUCCEEDED(hr)){void *a=nullptr,*b=nullptr,*sa=nullptr,*sb=nullptr;DWORD an=0,bn=0,san=0,sbn=0;hr=source->Lock(0,caps.dwBufferBytes,&sa,&san,&sb,&sbn,DSBLOCK_ENTIREBUFFER);if(SUCCEEDED(hr))hr=dest->Lock(0,caps.dwBufferBytes,&a,&an,&b,&bn,DSBLOCK_ENTIREBUFFER);if(SUCCEEDED(hr)){memcpy(a,sa,an);if(bn)memcpy(b,sb,bn);HRESULT unlock=dest->Unlock(a,an,b,bn);if(SUCCEEDED(hr))hr=unlock;}if(sa)source->Unlock(sa,san,sb,sbn);dest->Release();}}}
        source->Release();return hr;
    }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND hwnd,DWORD level)override{if(!hwnd||level<DSSCL_NORMAL||level>DSSCL_WRITEPRIMARY)return DSERR_INVALIDPARAM;return S_OK;}
    HRESULT STDMETHODCALLTYPE Compact()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD out)override{if(!out)return DSERR_INVALIDPARAM;*out=DSSPEAKER_COMBINED(DSSPEAKER_STEREO,DSSPEAKER_GEOMETRY_WIDE);return S_OK;}
    HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD config)override{return DSSPEAKER_CONFIG(config)==DSSPEAKER_STEREO||DSSPEAKER_CONFIG(config)==DSSPEAKER_HEADPHONE?S_OK:DSERR_UNSUPPORTED;}
    HRESULT STDMETHODCALLTYPE Initialize(LPCGUID)override{return DSERR_ALREADYINITIALIZED;}
    HRESULT STDMETHODCALLTYPE VerifyCertification(LPDWORD out)override{if(!out)return DSERR_INVALIDPARAM;*out=DS_UNCERTIFIED;return S_OK;}
private:
    LONG refs_;
};

static HRESULT enumerate_a(LPDSENUMCALLBACKA callback,LPVOID context){if(!callback)return DSERR_INVALIDPARAM;if(FAILED(ensure_engine()))return DSERR_NODRIVER;callback(&default_playback_id,"Default Audio Device","XAudio2 default render device",context);return S_OK;}
static HRESULT enumerate_w(LPDSENUMCALLBACKW callback,LPVOID context){if(!callback)return DSERR_INVALIDPARAM;if(FAILED(ensure_engine()))return DSERR_NODRIVER;callback(&default_playback_id,L"Default Audio Device",L"XAudio2 default render device",context);return S_OK;}
static HRESULT create_device(LPCGUID id,LPDIRECTSOUND8 *out,IUnknown *outer){if(!out)return E_POINTER;*out=nullptr;if(outer)return DSERR_NOAGGREGATION;if(id&&!IsEqualGUID(*id,default_playback_id))return DSERR_NODRIVER;if(FAILED(ensure_engine()))return DSERR_NODRIVER;void *mem=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Device));if(!mem)return E_OUTOFMEMORY;*out=new(mem)Device;return S_OK;}
}

extern "C" __declspec(dllexport) HRESULT WINAPI DirectSoundCreate(LPCGUID id,LPDIRECTSOUND *out,LPUNKNOWN outer){return create_device(id,reinterpret_cast<LPDIRECTSOUND8*>(out),outer);}
extern "C" __declspec(dllexport) HRESULT WINAPI DirectSoundCreate8(LPCGUID id,LPDIRECTSOUND8 *out,LPUNKNOWN outer){return create_device(id,out,outer);}
extern "C" __declspec(dllexport) HRESULT WINAPI DirectSoundEnumerateA(LPDSENUMCALLBACKA cb,LPVOID ctx){return enumerate_a(cb,ctx);}
extern "C" __declspec(dllexport) HRESULT WINAPI DirectSoundEnumerateW(LPDSENUMCALLBACKW cb,LPVOID ctx){return enumerate_w(cb,ctx);}
extern "C" __declspec(dllexport) HRESULT WINAPI GetDeviceID(LPCGUID source,LPGUID destination){if(!source||!destination)return DSERR_INVALIDPARAM;if(IsEqualGUID(*source,default_playback_id)){*destination=default_playback_id;return S_OK;}return DSERR_NODRIVER;}
extern "C" __declspec(dllexport) HRESULT WINAPI DirectSoundCaptureEnumerateW(LPDSENUMCALLBACKW,LPVOID){return DSERR_NODRIVER;}
