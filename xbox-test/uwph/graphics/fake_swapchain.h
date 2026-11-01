// Host regression fixture; no GPU.
struct FakeSwapChain : IDXGISwapChain1 {
    ULONG refs=1;
    HRESULT descResult=S_OK;
    UINT presents=0,lastInterval=0,lastFlags=0,lastWidth=0,lastHeight=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override { if(!out)return E_POINTER;*out=nullptr;return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a,UINT b,const void* c) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a,const IUnknown* b) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a,UINT* b,void* c) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id,void** out) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id,void** out) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Present(UINT interval,UINT flags) override { lastInterval=interval;lastFlags=flags;++presents;return DXGI_STATUS_OCCLUDED; }
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT index,REFIID id,void** out) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fullscreen,IDXGIOutput* output) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* fullscreen,IDXGIOutput** output) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* desc) override { if(FAILED(descResult))return descResult;
        *desc={};desc->BufferDesc.Width=1280;desc->BufferDesc.Height=720;
        desc->BufferDesc.RefreshRate={60000,1001};
        desc->BufferDesc.ScanlineOrdering=DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
        desc->BufferDesc.Scaling=DXGI_MODE_SCALING_CENTERED;return descResult; }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags) override { lastWidth=width;lastHeight=height;return DXGI_ERROR_DEVICE_RESET; }
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** out) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* out) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* out) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* out) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* out) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* out) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID id,void** out) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Present1(UINT interval,UINT flags,const DXGI_PRESENT_PARAMETERS* parameters) override { return E_NOTIMPL; }
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return FALSE; }
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** out) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* color) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* color) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION rotation) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* rotation) override { return E_NOTIMPL; }
};
