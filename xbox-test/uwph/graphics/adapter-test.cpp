#include "route.h"
#include "fake_factory.h"
#include <cstdio>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
struct FakeAdapter : IDXGIAdapter1 {
    ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=__uuidof(IDXGIAdapter1))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {return --refs;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID,void**) override {return E_UNEXPECTED;}
    HRESULT STDMETHODCALLTYPE EnumOutputs(UINT,IDXGIOutput** out) override {*out=nullptr;return DXGI_ERROR_NOT_FOUND;}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_ADAPTER_DESC* out) override {*out={};out->VendorId=0x1414;return S_OK;}
    HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(REFGUID,LARGE_INTEGER*) override {return DXGI_ERROR_UNSUPPORTED;}
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_ADAPTER_DESC1* out) override {*out={};out->VendorId=0x1414;out->AdapterLuid.LowPart=77;return S_OK;}
};
struct AdapterFactory : FakeFactory {
    FakeAdapter adapter;
    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT index,IDXGIAdapter** out) override {
        *out=nullptr;if(index)return DXGI_ERROR_NOT_FOUND;
        adapter.AddRef();*out=&adapter;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT index,IDXGIAdapter1** out) override {
        *out=nullptr;if(index)return DXGI_ERROR_NOT_FOUND;
        adapter.AddRef();*out=&adapter;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE,IDXGIAdapter** out) override {
        return EnumAdapters(0,out);
    }
};
static IDXGIAdapter* observed;
static UINT observedFlags;
static HRESULT WINAPI device(IDXGIAdapter* adapter,D3D_DRIVER_TYPE type,HMODULE software,UINT flags,
    const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,ID3D11Device** out,
    D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context) {
    observed=adapter;observedFlags=flags;
    if(type!=D3D_DRIVER_TYPE_UNKNOWN || software || !levels || count!=1 ||
       levels[0]!=D3D_FEATURE_LEVEL_11_0 || sdk!=D3D11_SDK_VERSION || !out || !level || !context)return E_UNEXPECTED;
    *out=nullptr;*context=nullptr;*level=D3D_FEATURE_LEVEL_11_0;
    return DXGI_ERROR_DEVICE_REMOVED;
}
static HRESULT WINAPI device12(IUnknown* adapter,D3D_FEATURE_LEVEL level,REFIID id,void** out) {
    observed=static_cast<IDXGIAdapter*>(adapter);
    if(level!=D3D_FEATURE_LEVEL_11_0 || id!=__uuidof(ID3D11Device))return E_UNEXPECTED;
    if(out)*out=nullptr;
    return DXGI_ERROR_DEVICE_REMOVED;
}
int main() {
    AdapterFactory native;
    {
        graphics_core::Factory factory(&native);
        CHECK(factory.EnumAdapters(0,nullptr)==DXGI_ERROR_INVALID_CALL);
        CHECK(factory.EnumAdapters1(0,nullptr)==DXGI_ERROR_INVALID_CALL);
        CHECK(factory.CreateSoftwareAdapter(nullptr,nullptr)==DXGI_ERROR_INVALID_CALL);
        IDXGIAdapter* old=reinterpret_cast<IDXGIAdapter*>(1);
        CHECK(factory.EnumAdapters(1,&old)==DXGI_ERROR_NOT_FOUND && !old);
        IDXGIAdapter1* modern=reinterpret_cast<IDXGIAdapter1*>(1);
        CHECK(factory.EnumAdapters1(1,&modern)==DXGI_ERROR_NOT_FOUND && !modern);
        for(unsigned path=0;path<3;++path) {
            IDXGIAdapter* adapter=nullptr;
            if(path==0)CHECK(factory.EnumAdapters(0,&adapter)==S_OK);
            else if(path==1){CHECK(factory.EnumAdapters1(0,&modern)==S_OK);adapter=modern;}
            else CHECK(factory.CreateSoftwareAdapter(nullptr,&adapter)==S_OK);
            CHECK(adapter!=&native.adapter && factory.refs==2 && native.adapter.refs==2);
            void* parent=nullptr;
            CHECK(adapter->GetParent(__uuidof(IDXGIFactory2),&parent)==S_OK && parent==&factory);
            CHECK(factory.Release()==2);
            CHECK(adapter->QueryInterface(__uuidof(IDXGIAdapter1),(void**)&modern)==S_OK);
            DXGI_ADAPTER_DESC1 desc{};
            CHECK(modern->GetDesc1(&desc)==S_OK && desc.VendorId==0x1414 && desc.AdapterLuid.LowPart==77);
            modern->Release();
            DXGI_ADAPTER_DESC legacy{};
            CHECK(adapter->GetDesc(&legacy)==S_OK && legacy.VendorId==0x1414);
            IDXGIOutput* output=reinterpret_cast<IDXGIOutput*>(1);
            CHECK(adapter->EnumOutputs(0,&output)==DXGI_ERROR_NOT_FOUND && !output);
            CHECK(adapter->GetParent(__uuidof(IDXGISwapChain),&parent)==E_NOINTERFACE && !parent);
            D3D_FEATURE_LEVEL requested=D3D_FEATURE_LEVEL_11_0,actual{};
            ID3D11Device* created=reinterpret_cast<ID3D11Device*>(1);
            ID3D11DeviceContext* context=reinterpret_cast<ID3D11DeviceContext*>(1);
            CHECK(graphics_core::create_device(device,adapter,D3D_DRIVER_TYPE_UNKNOWN,nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,&requested,1,D3D11_SDK_VERSION,&created,&actual,&context)==DXGI_ERROR_DEVICE_REMOVED);
            CHECK(observed==&native.adapter && observedFlags==D3D11_CREATE_DEVICE_BGRA_SUPPORT);
            CHECK(!created && !context && actual==requested);
            void* created12=reinterpret_cast<void*>(1);
            CHECK(graphics_core::create_device12(device12,adapter,requested,__uuidof(ID3D11Device),&created12)==DXGI_ERROR_DEVICE_REMOVED);
            CHECK(observed==&native.adapter && !created12);
            CHECK(graphics_core::create_device12(device12,&native.adapter,requested,__uuidof(ID3D11Device),nullptr)==DXGI_ERROR_DEVICE_REMOVED);
            CHECK(observed==&native.adapter);
            CHECK(adapter->Release()==0 && factory.refs==1 && native.adapter.refs==1);
            CHECK(graphics_core::adapters.empty());
        }
        CHECK(graphics_core::native_adapter(&native.adapter)==&native.adapter);
        CHECK(!graphics_core::native_adapter(nullptr));
        CHECK(graphics_core::resolve("D3D11.DLL","D3D11CreateDevice"));
        CHECK(graphics_core::resolve("d3d12.dll",nullptr,101)==reinterpret_cast<uintptr_t>(&graphics_core::d3d12_device));
        CHECK(graphics_core::resolve("D3D12.DLL","D3D12CreateDevice"));
        CHECK(!graphics_core::resolve("d3d12.dll",nullptr,102));
        CHECK(!graphics_core::resolve("dxgi.dll",nullptr,101));
    }
    CHECK(native.refs==0 && native.adapter.refs==1);
    // The enumerated adapter retains its bridge even after guest factory release.
    AdapterFactory retained;
    auto parent=new graphics_core::Factory(&retained);
    IDXGIAdapter* child=nullptr;
    CHECK(parent->EnumAdapters(0,&child)==S_OK);
    CHECK(parent->Release()==1 && retained.refs==1);
    void* identity=nullptr;
    CHECK(child->GetParent(__uuidof(IDXGIFactory2),&identity)==S_OK && identity==parent);
    CHECK(parent->Release()==1);
    CHECK(child->Release()==0 && retained.refs==0 && retained.adapter.refs==1);
    CHECK(graphics_core::adapters.empty());
    puts("graphics adapters: enumeration, parent identity, references and native D3D11/D3D12 argument/error preservation PASS");
}
