#pragma once
#include <dxgi1_2.h>
#include <mutex>
#include <unordered_map>
#include <new>
namespace graphics_core {
inline std::mutex output_mutex;
inline std::unordered_map<IDXGIOutput*,IDXGIOutput*> outputs;
// Retain the bridge parent while keeping display queries on the native output.
struct Output final : IDXGIOutput {
    IDXGIOutput* real;
    IUnknown* adapter;
    volatile LONG refs=1;
    Output(IDXGIOutput* value,IUnknown* parent):real(value),adapter(parent) {
        std::lock_guard<std::mutex> lock(output_mutex);
        outputs.emplace(this,real);
        adapter->AddRef();
    }
    ~Output() {
        {std::lock_guard<std::mutex> lock(output_mutex);outputs.erase(this);}
        real->Release();adapter->Release();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(id!=__uuidof(IUnknown) && id!=__uuidof(IDXGIObject) && id!=__uuidof(IDXGIOutput))return E_NOINTERFACE;
        *out=static_cast<IDXGIOutput*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override {ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a,UINT b,const void* c) override {return real->SetPrivateData(a,b,c);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a,const IUnknown* b) override {return real->SetPrivateDataInterface(a,b);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a,UINT* b,void* c) override {return real->GetPrivateData(a,b,c);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id,void** out) override {return adapter->QueryInterface(id,out);}
    void STDMETHODCALLTYPE ReleaseOwnership() override {real->ReleaseOwnership();}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_OUTPUT_DESC* a) override {return real->GetDesc(a);}
    HRESULT STDMETHODCALLTYPE GetDisplayModeList(DXGI_FORMAT a,UINT b,UINT* c,DXGI_MODE_DESC* d) override {return real->GetDisplayModeList(a,b,c,d);}
    HRESULT STDMETHODCALLTYPE FindClosestMatchingMode(const DXGI_MODE_DESC* a,DXGI_MODE_DESC* b,IUnknown* c) override {return real->FindClosestMatchingMode(a,b,c);}
    HRESULT STDMETHODCALLTYPE WaitForVBlank() override {return real->WaitForVBlank();}
    HRESULT STDMETHODCALLTYPE TakeOwnership(IUnknown* a,BOOL b) override {return real->TakeOwnership(a,b);}
    HRESULT STDMETHODCALLTYPE GetGammaControlCapabilities(DXGI_GAMMA_CONTROL_CAPABILITIES* a) override {return real->GetGammaControlCapabilities(a);}
    HRESULT STDMETHODCALLTYPE SetGammaControl(const DXGI_GAMMA_CONTROL* a) override {return real->SetGammaControl(a);}
    HRESULT STDMETHODCALLTYPE GetGammaControl(DXGI_GAMMA_CONTROL* a) override {return real->GetGammaControl(a);}
    HRESULT STDMETHODCALLTYPE SetDisplaySurface(IDXGISurface* a) override {return real->SetDisplaySurface(a);}
    HRESULT STDMETHODCALLTYPE GetDisplaySurfaceData(IDXGISurface* a) override {return real->GetDisplaySurfaceData(a);}
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* a) override {return real->GetFrameStatistics(a);}
};
inline IDXGIOutput* native_output(IDXGIOutput* value) {
    std::lock_guard<std::mutex> lock(output_mutex);
    auto found=outputs.find(value);
    return found==outputs.end()?value:found->second;
}
inline HRESULT wrap_output(IDXGIOutput* native,IUnknown* adapter,IDXGIOutput** out) {
    Output* wrapped=nullptr;
    try {wrapped=new(std::nothrow) Output(native,adapter);}
    catch(const std::bad_alloc&) {native->Release();return E_OUTOFMEMORY;}
    if(!wrapped){native->Release();return E_OUTOFMEMORY;}
    *out=wrapped;return S_OK;
}
}
