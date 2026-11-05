#pragma once
// Shared native factory adapter; the owner validates every guest HWND.
namespace corewindow_dxgi {
// The console exposes no DXGI outputs to apps, so Unity's Screen.resolutions
// stayed empty and a game indexing it (Megabonk's resolution 0) failed during
// startup. Adapters handed out by this factory therefore report one virtual
// output for the CoreWindow display when the real adapter has none (off-device
// hosts keep their real outputs). It only describes the window's display mode;
// it cannot take ownership, change gamma or present by itself.
// {6c3b2c71-5c3d-4d1e-9b0e-6e7a1f2d0a11}: private query returning the real adapter.
static const GUID realAdapterId={0x6c3b2c71,0x5c3d,0x4d1e,{0x9b,0x0e,0x6e,0x7a,0x1f,0x2d,0x0a,0x11}};
struct Output final : IDXGIOutput {
    ULONG refs=1; IDXGIAdapter* parent; UINT width, height;
    Output(IDXGIAdapter* a,UINT w,UINT h):parent(a),width(w),height(h){parent->AddRef();}
    ~Output(){parent->Release();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDXGIObject)||id==__uuidof(IDXGIOutput)){*out=static_cast<IDXGIOutput*>(this);AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n=InterlockedDecrement(&refs); if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT* n,void*) override { if(n)*n=0; return DXGI_ERROR_NOT_FOUND; }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id,void** out) override { return parent->QueryInterface(id,out); }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_OUTPUT_DESC* d) override {
        if(!d)return E_INVALIDARG;
        *d={};wcscpy_s(d->DeviceName,L"\\\\.\\DISPLAY1");
        d->DesktopCoordinates={0,0,LONG(width),LONG(height)};d->AttachedToDesktop=TRUE;
        d->Rotation=DXGI_MODE_ROTATION_IDENTITY;d->Monitor=nullptr;
        return S_OK;
    }
    static bool displayable(DXGI_FORMAT f) {
        return f==DXGI_FORMAT_R8G8B8A8_UNORM||f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||
               f==DXGI_FORMAT_B8G8R8A8_UNORM||f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||f==DXGI_FORMAT_R10G10B10A2_UNORM;
    }
    DXGI_MODE_DESC mode(DXGI_FORMAT f) const {
        DXGI_MODE_DESC m{};m.Width=width;m.Height=height;m.RefreshRate={60,1};m.Format=f;
        m.ScanlineOrdering=DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;m.Scaling=DXGI_MODE_SCALING_UNSPECIFIED;return m;
    }
    HRESULT STDMETHODCALLTYPE GetDisplayModeList(DXGI_FORMAT f,UINT,UINT* count,DXGI_MODE_DESC* modes) override {
        if(!count)return DXGI_ERROR_INVALID_CALL;
        UINT n=displayable(f)?1:0;
        if(!modes){*count=n;return S_OK;}
        if(*count<n){*count=0;return DXGI_ERROR_MORE_DATA;}
        if(n)modes[0]=mode(f);
        *count=n;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FindClosestMatchingMode(const DXGI_MODE_DESC* want,DXGI_MODE_DESC* closest,IUnknown*) override {
        if(!want||!closest)return DXGI_ERROR_INVALID_CALL;
        *closest=mode(displayable(want->Format)?want->Format:DXGI_FORMAT_B8G8R8A8_UNORM);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE WaitForVBlank() override { Sleep(16); return S_OK; }
    HRESULT STDMETHODCALLTYPE TakeOwnership(IUnknown*,BOOL) override { return DXGI_ERROR_UNSUPPORTED; }
    void STDMETHODCALLTYPE ReleaseOwnership() override {}
    HRESULT STDMETHODCALLTYPE GetGammaControlCapabilities(DXGI_GAMMA_CONTROL_CAPABILITIES*) override { return DXGI_ERROR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE SetGammaControl(const DXGI_GAMMA_CONTROL*) override { return DXGI_ERROR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetGammaControl(DXGI_GAMMA_CONTROL*) override { return DXGI_ERROR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE SetDisplaySurface(IDXGISurface*) override { return DXGI_ERROR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetDisplaySurfaceData(IDXGISurface*) override { return DXGI_ERROR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS*) override { return DXGI_ERROR_FRAME_STATISTICS_DISJOINT; }
};
struct Adapter final : IDXGIAdapter1 {
    ULONG refs=1; IDXGIAdapter1* real; UINT width, height;
    Adapter(IDXGIAdapter1* a,UINT w,UINT h):real(a),width(w),height(h){}
    ~Adapter(){real->Release();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDXGIObject)||id==__uuidof(IDXGIAdapter)||id==__uuidof(IDXGIAdapter1)){
            *out=static_cast<IDXGIAdapter1*>(this);AddRef();return S_OK;}
        if(id==realAdapterId){real->AddRef();*out=real;return S_OK;}
        return real->QueryInterface(id,out);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n=InterlockedDecrement(&refs); if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a,UINT b,const void* c) override { return real->SetPrivateData(a,b,c); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a,const IUnknown* b) override { return real->SetPrivateDataInterface(a,b); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a,UINT* b,void* c) override { return real->GetPrivateData(a,b,c); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID a,void** b) override { return real->GetParent(a,b); }
    HRESULT STDMETHODCALLTYPE EnumOutputs(UINT index,IDXGIOutput** out) override {
        if(!out)return E_INVALIDARG;*out=nullptr;
        IDXGIOutput* first=nullptr;HRESULT hr=real->EnumOutputs(0,&first);
        if(SUCCEEDED(hr)){first->Release();return real->EnumOutputs(index,out);} // real outputs exist
        if(hr!=DXGI_ERROR_NOT_FOUND)return hr;
        if(index)return DXGI_ERROR_NOT_FOUND;
        *out=new Output(this,width,height);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_ADAPTER_DESC* d) override { return real->GetDesc(d); }
    HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(REFGUID a,LARGE_INTEGER* b) override { return real->CheckInterfaceSupport(a,b); }
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_ADAPTER_DESC1* d) override { return real->GetDesc1(d); }
};
// D3D11CreateDevice must receive the real adapter, not a wrapper.
inline IDXGIAdapter* real_adapter(IDXGIAdapter* a) {
    if(!a)return nullptr;IDXGIAdapter* real=nullptr;
    return SUCCEEDED(a->QueryInterface(realAdapterId,(void**)&real))?real:nullptr;
}
inline UINT outputWidth=1920, outputHeight=1080;
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
    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT a,IDXGIAdapter** b) override {
        if(!b)return E_INVALIDARG;IDXGIAdapter1* one=nullptr;HRESULT hr=EnumAdapters1(a,&one);*b=one;return hr; }
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
    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT a,IDXGIAdapter1** b) override {
        if(!b)return E_INVALIDARG;*b=nullptr;IDXGIAdapter1* adapter=nullptr;HRESULT hr=real->EnumAdapters1(a,&adapter);
        if(FAILED(hr))return hr;*b=new Adapter(adapter,outputWidth,outputHeight);return S_OK; }
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
