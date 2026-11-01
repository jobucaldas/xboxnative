#pragma once
#include <dxgi1_2.h>
#include "output.h"
#include <mutex>
#include <new>
#include <unordered_map>
namespace graphics_core {
// Only adapters issued by this bridge are unwrapped at the D3D device boundary.
// Unknown pointers are native COM arguments and are left to the native API.
inline std::mutex adapter_mutex;
inline std::unordered_map<IDXGIAdapter*, IDXGIAdapter1*> adapters;
struct Adapter final : IDXGIAdapter1 {
    IDXGIAdapter1* real;
    IUnknown* factory;
    volatile LONG refs=1;
    Adapter(IDXGIAdapter1* native,IUnknown* parent):real(native),factory(parent) {
        std::lock_guard<std::mutex> lock(adapter_mutex);
        adapters.emplace(this,real);
        factory->AddRef();
    }
    ~Adapter() {
        {std::lock_guard<std::mutex> lock(adapter_mutex);adapters.erase(this);}
        real->Release();factory->Release();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(id==__uuidof(IUnknown) || id==__uuidof(IDXGIObject) ||
           id==__uuidof(IDXGIAdapter) || id==__uuidof(IDXGIAdapter1)) {
            *out=static_cast<IDXGIAdapter1*>(this);AddRef();return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override {ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a,UINT b,const void* c) override {return real->SetPrivateData(a,b,c);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a,const IUnknown* b) override {return real->SetPrivateDataInterface(a,b);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a,UINT* b,void* c) override {return real->GetPrivateData(a,b,c);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id,void** out) override {return factory->QueryInterface(id,out);}
    HRESULT STDMETHODCALLTYPE EnumOutputs(UINT index,IDXGIOutput** out) override {
        if(!out)return DXGI_ERROR_INVALID_CALL;
        *out=nullptr;
        IDXGIOutput* native=nullptr;
        HRESULT hr=real->EnumOutputs(index,&native);
        if(FAILED(hr))return hr;
        if(!native)return E_UNEXPECTED;
        return wrap_output(native,this,out);
    }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_ADAPTER_DESC* out) override {return real->GetDesc(out);}
    HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(REFGUID id,LARGE_INTEGER* version) override {return real->CheckInterfaceSupport(id,version);}
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_ADAPTER_DESC1* out) override {return real->GetDesc1(out);}
};
inline IDXGIAdapter* native_adapter(IDXGIAdapter* value) {
    std::lock_guard<std::mutex> lock(adapter_mutex);
    auto found=adapters.find(value);
    return found==adapters.end()?value:found->second;
}
inline HRESULT wrap_adapter(IDXGIAdapter* native,IUnknown* factory,IDXGIAdapter1** out) {
    IDXGIAdapter1* modern=nullptr;
    HRESULT hr=native->QueryInterface(__uuidof(IDXGIAdapter1),(void**)&modern);
    native->Release();
    if(FAILED(hr))return hr;
    Adapter* wrapped=nullptr;
    try {wrapped=new(std::nothrow) Adapter(modern,factory);}
    catch(const std::bad_alloc&) {modern->Release();return E_OUTOFMEMORY;}
    if(!wrapped){modern->Release();return E_OUTOFMEMORY;}
    *out=wrapped;return S_OK;
}
// Consume a native output reference and rebuild both parent links. A native
// swapchain may return a different adapter from the one used at enumeration.
inline HRESULT wrap_chain_output(IDXGIOutput* native,IUnknown* factory,IDXGIOutput** out) {
    IDXGIAdapter* parent=nullptr;
    HRESULT hr=native->GetParent(__uuidof(IDXGIAdapter),(void**)&parent);
    if(FAILED(hr)){native->Release();return hr;}
    if(!parent){native->Release();return E_UNEXPECTED;}
    IDXGIAdapter1* adapter=nullptr;
    hr=wrap_adapter(parent,factory,&adapter);
    if(FAILED(hr)){native->Release();return hr;}
    hr=wrap_output(native,adapter,out);
    adapter->Release();
    return hr;
}

}
