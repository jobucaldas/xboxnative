#pragma once
// UI-thread DXGI factory route. Never send an opaque w32u HWND to system DXGI.
namespace swc {
static IUnknown* (*corewindow)(HWND);
static HMODULE display;
struct Factory final : IDXGIFactory2 {
    IDXGIFactory2* real;
    ULONG refs = 1;
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
        HRESULT hr=real->CreateSwapChainForCoreWindow(d,c,s,r,out); c->Release();
        logf("SWC route HWND -> CoreWindow hr=0x%08lx",(unsigned long)hr); return hr;
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
static HRESULT WINAPI factory(REFIID id,void** out) {
    if(!out)return E_POINTER; *out=nullptr;
    IDXGIFactory2* real=nullptr; HRESULT hr=CreateDXGIFactory1(__uuidof(IDXGIFactory2),(void**)&real);
    if(FAILED(hr))return hr;
    auto f=new Factory(real); hr=f->QueryInterface(id,out); f->Release(); return hr;
}
static uintptr_t resolve(const char* dll,const char* name,WORD ordinal) {
    if(!name || ordinal)return 0;
    if(!_stricmp(dll,"user32.dll"))return (uintptr_t)GetProcAddress(display,name);
    if(!_stricmp(dll,"dxgi.dll") && (!strcmp(name,"CreateDXGIFactory1") || !strcmp(name,"CreateDXGIFactory")))return (uintptr_t)&factory;
    if(!_stricmp(dll,"d3d11.dll") && !strcmp(name,"D3D11CreateDevice"))return (uintptr_t)&D3D11CreateDevice;
    for(auto& b:wxl::builtins)if(!_stricmp(dll,b.dll)&&!strcmp(name,b.name))return b.address;
    return 0;
}
static void run(IUnknown* window,const wchar_t* path) {
    display=LoadPackagedLibrary(L"uwph-user32.dll",0);
    using Init=void(*)(IUnknown*,void(*)(),LONG,LONG);
    auto init=display?(Init)GetProcAddress(display,"uwph_display_init"):nullptr;
    corewindow=display?(IUnknown*(*)(HWND))GetProcAddress(display,"uwph_corewindow"):nullptr;
    if(!init || !corewindow){logf("SWC FAIL display exports");return;}
    init(window,[]{ CoreWindow::GetForCurrentThread().Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent); },1920,1080);
    // Guest callbacks and retained D3D objects live until process termination.
    static wxl::Image image;
    if(!wxl::run(path,test_log,&image,resolve)) {logf("SWC FAIL mapping");return;}
    unsigned rwx=0;
    for(size_t offset=0;offset<image.size;offset+=4096) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(image.base+offset,&info,sizeof(info))) {logf("SWC FAIL VirtualQuery");return;}
        if((info.Protect & 0xff)==PAGE_EXECUTE_READWRITE || (info.Protect & 0xff)==PAGE_EXECUTE_WRITECOPY)++rwx;
    }
    logf("SWC mapped pages=%zu RWX=%u",image.size/4096,rwx);
    if(rwx)return;
    auto dos=(IMAGE_DOS_HEADER*)image.base;
    auto nt=(IMAGE_NT_HEADERS64*)(image.base+dos->e_lfanew);
    DWORD result=((DWORD(WINAPI*)())(image.base+nt->OptionalHeader.AddressOfEntryPoint))();
    logf("SWC RESULT entry=%lu failures=%d",result,result==42?0:1);
}
}
