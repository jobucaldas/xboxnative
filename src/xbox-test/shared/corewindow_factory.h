#pragma once
// Shared native factory adapter; the owner validates every guest HWND.
namespace corewindow_dxgi {
using PresentSurface = HRESULT (*)(IDXGIFactory2*, IUnknown*, IUnknown*,
    const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**);
struct Factory final : IDXGIFactory2 {
    IDXGIFactory2* real;
    ULONG refs = 1;
    IUnknown* (*corewindow)(HWND);
    void (*report)(HRESULT);
    PresentSurface createSurface;
    Factory(IDXGIFactory2* f, IUnknown* (*window)(HWND), void (*logger)(HRESULT),
            PresentSurface create = nullptr)
        : real(f), corewindow(window), report(logger), createSurface(create) {}
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
    HRESULT STDMETHODCALLTYPE GetParent(REFIID,void** p) override { if(p)*p=nullptr; return E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT a,IDXGIAdapter** b) override { return real->EnumAdapters(a,b); }
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND h,UINT) override { auto c=corewindow(h); if(!c)return DXGI_ERROR_INVALID_CALL; c->Release(); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND* h) override { if(!h)return E_POINTER; *h=nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown* d,DXGI_SWAP_CHAIN_DESC* s,IDXGISwapChain** out) override {
        if(!out)return E_POINTER; *out=nullptr;
        if(!s || !s->Windowed || s->Flags)return DXGI_ERROR_INVALID_CALL;
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
    HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE a,IDXGIAdapter** b) override { return real->CreateSoftwareAdapter(a,b); }
    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT a,IDXGIAdapter1** b) override { return real->EnumAdapters1(a,b); }
    BOOL STDMETHODCALLTYPE IsCurrent() override { return real->IsCurrent(); }
    BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled() override { return real->IsWindowedStereoEnabled(); }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown* d,HWND h,const DXGI_SWAP_CHAIN_DESC1* s,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* f,IDXGIOutput* r,IDXGISwapChain1** out) override {
        if(!out)return E_POINTER; *out=nullptr;
        if(!s || (f && !f->Windowed))return DXGI_ERROR_INVALID_CALL;
        IUnknown* c=corewindow(h); if(!c)return DXGI_ERROR_INVALID_CALL;
        HRESULT hr=createSurface ? createSurface(real,d,c,s,r,out)
            : real->CreateSwapChainForCoreWindow(d,c,s,r,out); c->Release();
        if(report)report(hr); return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown* a,IUnknown* b,const DXGI_SWAP_CHAIN_DESC1* c,IDXGIOutput* d,IDXGISwapChain1** e) override { return real->CreateSwapChainForCoreWindow(a,b,c,d,e); }
    HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE a,LUID* b) override { return real->GetSharedResourceAdapterLuid(a,b); }
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND,UINT,DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE a,DWORD* b) override { return real->RegisterStereoStatusEvent(a,b); }
    void STDMETHODCALLTYPE UnregisterStereoStatus(DWORD a) override { real->UnregisterStereoStatus(a); }
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(HWND,UINT,DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(HANDLE a,DWORD* b) override { return real->RegisterOcclusionStatusEvent(a,b); }
    void STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD a) override { real->UnregisterOcclusionStatus(a); }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown* a,const DXGI_SWAP_CHAIN_DESC1* b,IDXGIOutput* c,IDXGISwapChain1** d) override { return real->CreateSwapChainForComposition(a,b,c,d); }
};
}
