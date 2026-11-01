#pragma once
// UI-thread DXGI factory route. Never send an opaque w32u HWND to system DXGI.
#include <windows.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <new>
#include <functional>
#include <atomic>
#include "swapchain.h"
#include "adapter.h"
#include <d3d11.h>
namespace graphics_core {
// The launcher installs these hooks before starting any guest worker. Window
// validation uses the owning record registry; creation runs on the UI thread.
using ValidWindow = BOOL (WINAPI*)(HWND);
using CreateChain = std::function<HRESULT(IDXGIFactory2*, IUnknown*, HWND,
    const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**)>;
inline ValidWindow valid_window = nullptr;
inline CreateChain create_chain = nullptr;
inline std::function<void()> release_chain;
inline void configure(ValidWindow valid, CreateChain create,std::function<void()> released={}) {
    valid_window = valid; create_chain = create; release_chain=std::move(released);
}
// Validate structure before the UI hook relinquishes the launcher swapchain.
// Wine 10.0 dxgi_validate_swapchain_desc applies these same effect/count/sample
// rules. Native DXGI still owns format, usage and device capability validation.
inline bool valid_swapchain_structure(DXGI_SWAP_EFFECT effect,UINT buffers,
                                     const DXGI_SAMPLE_DESC& samples,DXGI_FORMAT format) {
    switch(effect) {
    case DXGI_SWAP_EFFECT_DISCARD:
    case DXGI_SWAP_EFFECT_SEQUENTIAL:
        return buffers>=1 && buffers<=DXGI_MAX_SWAP_CHAIN_BUFFERS;
    case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
    case DXGI_SWAP_EFFECT_FLIP_DISCARD:
        // Match Wine's flip format set. UNKNOWN remains native validation's
        // responsibility (DXGI can infer the existing target format).
        if(format!=DXGI_FORMAT_UNKNOWN && format!=DXGI_FORMAT_R16G16B16A16_FLOAT &&
           format!=DXGI_FORMAT_R10G10B10A2_UNORM && format!=DXGI_FORMAT_R8G8B8A8_UNORM &&
           format!=DXGI_FORMAT_B8G8R8A8_UNORM)return false;
        return buffers>=2 && buffers<=DXGI_MAX_SWAP_CHAIN_BUFFERS &&
               samples.Count==1 && !samples.Quality;
    default:
        return false;
    }
}
struct Factory final : IDXGIFactory2 {
    IDXGIFactory2* real;
    volatile LONG refs = 1;
    std::atomic<HWND> associated{nullptr};
    explicit Factory(IDXGIFactory2* f) : real(f) {}
    ~Factory() { real->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IDXGIObject) || id == __uuidof(IDXGIFactory) ||
            id == __uuidof(IDXGIFactory1) || id == __uuidof(IDXGIFactory2)) {
            *out = static_cast<IDXGIFactory2*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a,UINT b,const void* c) override { return real->SetPrivateData(a,b,c); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a,const IUnknown* b) override { return real->SetPrivateDataInterface(a,b); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a,UINT* b,void* c) override { return real->GetPrivateData(a,b,c); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id,void** p) override { return real->GetParent(id,p); }
    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT index,IDXGIAdapter** out) override {
        if(!out)return DXGI_ERROR_INVALID_CALL;
        *out=nullptr;
        IDXGIAdapter* native=nullptr;
        HRESULT hr=real->EnumAdapters(index,&native);
        if(FAILED(hr))return hr;
        IDXGIAdapter1* wrapped=nullptr;
        hr=wrap_adapter(native,this,&wrapped);
        if(SUCCEEDED(hr))*out=wrapped;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND h,UINT flags) override { if (!valid_window || (h && !valid_window(h)) || flags & ~7u)
            return DXGI_ERROR_INVALID_CALL;
        associated = h; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND* h) override { if(!h)return E_POINTER; *h=associated.load(); return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown* d,DXGI_SWAP_CHAIN_DESC* s,IDXGISwapChain** out) override {
        if(!out)return E_POINTER; *out=nullptr;
        if(!s || !s->Windowed || s->Flags || !d)return DXGI_ERROR_INVALID_CALL;
        // Check the guest descriptor before raising legacy buffer counts for
        // CoreWindow. Otherwise invalid zero/one-buffer flip requests succeed.
        if(!valid_swapchain_structure(s->SwapEffect,s->BufferCount,s->SampleDesc,s->BufferDesc.Format))
            return DXGI_ERROR_INVALID_CALL;
        DXGI_SWAP_CHAIN_DESC1 v{}; v.Width=s->BufferDesc.Width; v.Height=s->BufferDesc.Height;
        v.Format=s->BufferDesc.Format; v.SampleDesc=s->SampleDesc; v.BufferUsage=s->BufferUsage;
        // CoreWindow needs flip presentation; desktop legacy discard is common.
        // Multisampled legacy buffers need a resolve layer, outside this route.
        if(s->SampleDesc.Count!=1 || s->SampleDesc.Quality)return DXGI_ERROR_INVALID_CALL;
        v.BufferCount=std::max(2u,s->BufferCount);
        v.SwapEffect=(s->SwapEffect==DXGI_SWAP_EFFECT_DISCARD || s->SwapEffect==DXGI_SWAP_EFFECT_SEQUENTIAL)
            ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL : s->SwapEffect;
        v.Scaling=DXGI_SCALING_STRETCH;
        v.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
        IDXGISwapChain1* chain=nullptr;
        HRESULT hr=CreateSwapChainForHwnd(d,s->OutputWindow,&v,nullptr,nullptr,&chain);
        if(SUCCEEDED(hr))*out=chain;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE module,IDXGIAdapter** out) override {
        if(!out)return DXGI_ERROR_INVALID_CALL;
        *out=nullptr;
        IDXGIAdapter* native=nullptr;
        HRESULT hr=real->CreateSoftwareAdapter(module,&native);
        if(FAILED(hr))return hr;
        IDXGIAdapter1* wrapped=nullptr;
        hr=wrap_adapter(native,this,&wrapped);
        if(SUCCEEDED(hr))*out=wrapped;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT index,IDXGIAdapter1** out) override {
        if(!out)return DXGI_ERROR_INVALID_CALL;
        *out=nullptr;
        IDXGIAdapter1* native=nullptr;
        HRESULT hr=real->EnumAdapters1(index,&native);
        if(FAILED(hr))return hr;
        return wrap_adapter(native,this,out);
    }
    BOOL STDMETHODCALLTYPE IsCurrent() override { return real->IsCurrent(); }
    BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled() override { return real->IsWindowedStereoEnabled(); }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown* d,HWND h,const DXGI_SWAP_CHAIN_DESC1* s,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* f,IDXGIOutput* r,IDXGISwapChain1** out) override {
        if(!out)return E_POINTER; *out=nullptr;
        if(!d || !s || (f && !f->Windowed) || !valid_window || !valid_window(h))
            return DXGI_ERROR_INVALID_CALL;
        if(!valid_swapchain_structure(s->SwapEffect,s->BufferCount,s->SampleDesc,s->Format))
            return DXGI_ERROR_INVALID_CALL;
        // HWND callers can request desktop blit presentation directly, without
        // going through CreateSwapChain. Apply the same CoreWindow conversion
        // as the legacy entry point, without changing the guest descriptor.
        DXGI_SWAP_CHAIN_DESC1 presentation=*s;
        if(presentation.SwapEffect==DXGI_SWAP_EFFECT_DISCARD ||
           presentation.SwapEffect==DXGI_SWAP_EFFECT_SEQUENTIAL) {
            presentation.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            presentation.BufferCount=std::max(2u,presentation.BufferCount);
            // No MSAA resolve or format reinterpretation layer exists here.
            if(!valid_swapchain_structure(presentation.SwapEffect,presentation.BufferCount,
                                         presentation.SampleDesc,presentation.Format))
                return DXGI_ERROR_INVALID_CALL;
        }
        if(!create_chain)return DXGI_ERROR_UNSUPPORTED;
        std::function<void()> released;
        try {released=release_chain;}
        catch(const std::bad_alloc&) {return E_OUTOFMEMORY;}
        IDXGISwapChain1* native=nullptr;
        HRESULT hr=create_chain(real,d,h,&presentation,native_output(r),&native);
        if(FAILED(hr)){if(native)native->Release();return hr;}
        if(!native)return E_UNEXPECTED;
        auto wrapped=new(std::nothrow) SwapChain(native,this,h,std::move(released));
        if(!wrapped){native->Release();if(released)released();return E_OUTOFMEMORY;}
        *out=wrapped;return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown* a,IUnknown* b,const DXGI_SWAP_CHAIN_DESC1* c,IDXGIOutput* d,IDXGISwapChain1** e) override { return real->CreateSwapChainForCoreWindow(a,b,c,native_output(d),e); }
    HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE a,LUID* b) override { return real->GetSharedResourceAdapterLuid(a,b); }
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND,UINT,DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE a,DWORD* b) override { return real->RegisterStereoStatusEvent(a,b); }
    void STDMETHODCALLTYPE UnregisterStereoStatus(DWORD a) override { real->UnregisterStereoStatus(a); }
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(HWND,UINT,DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(HANDLE a,DWORD* b) override { return real->RegisterOcclusionStatusEvent(a,b); }
    void STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD a) override { real->UnregisterOcclusionStatus(a); }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown* a,const DXGI_SWAP_CHAIN_DESC1* b,IDXGIOutput* c,IDXGISwapChain1** d) override { return real->CreateSwapChainForComposition(a,b,native_output(c),d); }
};
inline HRESULT WINAPI factory2(UINT flags, REFIID id,void** out) {
    if(!out)return E_POINTER; *out=nullptr;
    IDXGIFactory2* real=nullptr;
    // Resolve by name so older hosts report a real unsupported API instead of
    // importing a desktop-only entry at process load time.
    HMODULE module=GetModuleHandleW(L"dxgi.dll");
    using Create=HRESULT(WINAPI*)(UINT,REFIID,void**);
    auto create=module?reinterpret_cast<Create>(GetProcAddress(module,"CreateDXGIFactory2")):nullptr;
    HRESULT hr;
    if(create)hr=create(flags,__uuidof(IDXGIFactory2),(void**)&real);
    else if(flags) return DXGI_ERROR_UNSUPPORTED;
    else hr=CreateDXGIFactory1(__uuidof(IDXGIFactory2),(void**)&real);
    if(FAILED(hr))return hr;
    auto f=new(std::nothrow) Factory(real);
    if(!f){real->Release();return E_OUTOFMEMORY;}
    hr=f->QueryInterface(id,out); f->Release(); report("CreateDXGIFactory QI",hr,id.Data1); return hr;
}
inline HRESULT WINAPI factory(REFIID id,void** out) { return factory2(0,id,out); }
// Keep native D3D validation and errors; only translate bridge-owned adapters.
using CreateDevice=decltype(&D3D11CreateDevice);
inline HRESULT create_device(CreateDevice create,IDXGIAdapter* adapter,D3D_DRIVER_TYPE type,
    HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,
    ID3D11Device** device,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context) {
    return create(native_adapter(adapter),type,software,flags,levels,count,sdk,device,level,context);
}
inline HRESULT WINAPI d3d11_device(IDXGIAdapter* adapter,D3D_DRIVER_TYPE type,
    HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,
    ID3D11Device** device,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context) {
    HMODULE module=GetModuleHandleW(L"d3d11.dll");
    auto create=module?reinterpret_cast<CreateDevice>(GetProcAddress(module,"D3D11CreateDevice")):nullptr;
    if(!create)return DXGI_ERROR_UNSUPPORTED;
    return create_device(create,adapter,type,software,flags,levels,count,sdk,device,level,context);
}
using CreateDevice12=HRESULT(WINAPI*)(IUnknown*,D3D_FEATURE_LEVEL,REFIID,void**);
inline HRESULT create_device12(CreateDevice12 create,IUnknown* adapter,D3D_FEATURE_LEVEL level,
    REFIID id,void** out) {
    return create(native_adapter(static_cast<IDXGIAdapter*>(adapter)),level,id,out);
}
inline HRESULT WINAPI d3d12_device(IUnknown* adapter,D3D_FEATURE_LEVEL level,REFIID id,void** out) {
    HMODULE module=GetModuleHandleW(L"d3d12.dll");
    auto create=module?reinterpret_cast<CreateDevice12>(GetProcAddress(module,"D3D12CreateDevice")):nullptr;
    if(!create)return DXGI_ERROR_UNSUPPORTED;
    return create_device12(create,adapter,level,id,out);
}
inline uintptr_t resolve(const char* dll,const char* name,WORD ordinal=0) {
    if(!dll)return 0;
    // Megabonk imports D3D12CreateDevice by DLL ordinal.
    if(!_stricmp(dll,"d3d12.dll") && ((name && !strcmp(name,"D3D12CreateDevice")) || (!name && ordinal==101)))
        return reinterpret_cast<uintptr_t>(&d3d12_device);
    if(!name)return 0;
    if(!_stricmp(dll,"d3d11.dll") && !strcmp(name,"D3D11CreateDevice"))
        return reinterpret_cast<uintptr_t>(&d3d11_device);
    if(_stricmp(dll,"dxgi.dll"))return 0;
    if(!strcmp(name,"CreateDXGIFactory") || !strcmp(name,"CreateDXGIFactory1"))
        return reinterpret_cast<uintptr_t>(&factory);
    if(!strcmp(name,"CreateDXGIFactory2"))return reinterpret_cast<uintptr_t>(&factory2);
    return 0;
}
}
