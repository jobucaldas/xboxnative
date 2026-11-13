#define CINTERFACE
#include <windows.h>
#include <xaudio2.h>
#include <audioclient.h>
#include <cstddef>
#include <type_traits>

using XAudio2CreateFn = HRESULT (WINAPI *)(IXAudio2 **, UINT32, XAUDIO2_PROCESSOR);
static_assert(std::is_same<decltype(&XAudio2Create), XAudio2CreateFn>::value,
              "XAudio2Create signature changed");
using CreateMasteringVoiceFn = HRESULT (STDMETHODCALLTYPE *)(
    IXAudio2 *, IXAudio2MasteringVoice **, UINT32, UINT32, UINT32, LPCWSTR,
    const XAUDIO2_EFFECT_CHAIN *, AUDIO_STREAM_CATEGORY);
static_assert(std::is_same<decltype(IXAudio2Vtbl::CreateMasteringVoice), CreateMasteringVoiceFn>::value,
              "IXAudio2::CreateMasteringVoice signature changed");
using SubmitSourceBufferFn = HRESULT (STDMETHODCALLTYPE *)(
    IXAudio2SourceVoice *, const XAUDIO2_BUFFER *, const XAUDIO2_BUFFER_WMA *);
static_assert(std::is_same<decltype(IXAudio2SourceVoiceVtbl::SubmitSourceBuffer), SubmitSourceBufferFn>::value,
              "IXAudio2SourceVoice::SubmitSourceBuffer signature changed");
using GetStateFn = void (STDMETHODCALLTYPE *)(
    IXAudio2SourceVoice *, XAUDIO2_VOICE_STATE *, UINT32);
static_assert(std::is_same<decltype(IXAudio2SourceVoiceVtbl::GetState), GetStateFn>::value,
              "IXAudio2SourceVoice::GetState signature changed");
static_assert(offsetof(IXAudio2Vtbl, CreateMasteringVoice) == 7 * sizeof(void *),
              "IXAudio2::CreateMasteringVoice vtable slot changed");
static_assert(offsetof(IXAudio2SourceVoiceVtbl, SubmitSourceBuffer) == 21 * sizeof(void *),
              "IXAudio2SourceVoice::SubmitSourceBuffer vtable slot changed");
static_assert(offsetof(IXAudio2SourceVoiceVtbl, GetState) == 25 * sizeof(void *),
              "IXAudio2SourceVoice::GetState vtable slot changed");

using AudioInitializeFn = HRESULT (STDMETHODCALLTYPE *)(
    IAudioClient *, AUDCLNT_SHAREMODE, DWORD, REFERENCE_TIME, REFERENCE_TIME,
    const WAVEFORMATEX *, LPCGUID);
static_assert(std::is_same<decltype(IAudioClientVtbl::Initialize), AudioInitializeFn>::value,
              "IAudioClient::Initialize signature changed");
using AudioGetServiceFn = HRESULT (STDMETHODCALLTYPE *)(IAudioClient *, REFIID, void **);
static_assert(std::is_same<decltype(IAudioClientVtbl::GetService), AudioGetServiceFn>::value,
              "IAudioClient::GetService signature changed");
using RenderGetBufferFn = HRESULT (STDMETHODCALLTYPE *)(IAudioRenderClient *, UINT32, BYTE **);
static_assert(std::is_same<decltype(IAudioRenderClientVtbl::GetBuffer), RenderGetBufferFn>::value,
              "IAudioRenderClient::GetBuffer signature changed");
using RenderReleaseBufferFn = HRESULT (STDMETHODCALLTYPE *)(IAudioRenderClient *, UINT32, DWORD);
static_assert(std::is_same<decltype(IAudioRenderClientVtbl::ReleaseBuffer), RenderReleaseBufferFn>::value,
              "IAudioRenderClient::ReleaseBuffer signature changed");
static_assert(offsetof(IAudioClientVtbl, Initialize) == 3 * sizeof(void *),
              "IAudioClient::Initialize vtable slot changed");
static_assert(offsetof(IAudioClientVtbl, GetService) == 14 * sizeof(void *),
              "IAudioClient::GetService vtable slot changed");
static_assert(offsetof(IAudioRenderClientVtbl, GetBuffer) == 3 * sizeof(void *),
              "IAudioRenderClient::GetBuffer vtable slot changed");
static_assert(offsetof(IAudioRenderClientVtbl, ReleaseBuffer) == 4 * sizeof(void *),
              "IAudioRenderClient::ReleaseBuffer vtable slot changed");
