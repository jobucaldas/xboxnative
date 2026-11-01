// Host regression fixture: no device or display required.
struct FakeFactory : IDXGIFactory2 {
    ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override { if(!out)return E_POINTER; *out=nullptr; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a,UINT b,const void* c) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a,const IUnknown* b) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a,UINT* b,void* c) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id,void** p) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT a,IDXGIAdapter** b) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND h,UINT flags) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND* h) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown* d,DXGI_SWAP_CHAIN_DESC* s,IDXGISwapChain** out) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE a,IDXGIAdapter** b) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT a,IDXGIAdapter1** b) override { return E_NOTIMPL; }
    BOOL STDMETHODCALLTYPE IsCurrent() override { return FALSE; }
    BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled() override { return FALSE; }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown* d,HWND h,const DXGI_SWAP_CHAIN_DESC1* s,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* f,IDXGIOutput* r,IDXGISwapChain1** out) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown* a,IUnknown* b,const DXGI_SWAP_CHAIN_DESC1* c,IDXGIOutput* d,IDXGISwapChain1** e) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE a,LUID* b) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND,UINT,DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE a,DWORD* b) override { return E_NOTIMPL; }
    void STDMETHODCALLTYPE UnregisterStereoStatus(DWORD a) override {  }
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(HWND,UINT,DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(HANDLE a,DWORD* b) override { return E_NOTIMPL; }
    void STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD a) override {  }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown* a,const DXGI_SWAP_CHAIN_DESC1* b,IDXGIOutput* c,IDXGISwapChain1** d) override { return E_NOTIMPL; }
};
