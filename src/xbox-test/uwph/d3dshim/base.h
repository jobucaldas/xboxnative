// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
// Shared foundation for Play's D3D10 and D3D9 shims.
//
// The console has d3d11.dll and dxgi.dll but no d3d9.dll, d3d10.dll or
// d3d10_1.dll (issue #172). Wine's d3d10core is not a thin adapter
// over an arbitrary D3D11: dlls/d3d10core/d3d10core_main.c loads d3d11.dll and
// calls DXGID3D10CreateDevice, which registers Wine's d3d11core layer, and
// dlls/dxgi/device.c dxgi_device_init then calls wined3d_mutex_lock and
// wined3d_device_create, so it needs Wine's own dxgi, d3d11 and wined3d (GL)
// as well. These shims are Play's own code instead.
//
// Each shim is a PE DLL that Play maps with its own loader, so its static
// imports bind to Play's routes rather than to a native DLL: the
// d3d11.dll!D3D11CreateDevice import reaches Play's hook (guest adapter
// unwrapped, device adopted for guest-window swapchains) and
// dxgi.dll!CreateDXGIFactory1 reaches Play's validated CoreWindow factory.
// Never call LoadLibrary for those: a fresh load would find the console's real
// DLL and bypass the routes. Nothing here may assume a desktop host, so the
// shims are built without a CRT: no exceptions, no RTTI, no stdio.
//
// The shims link no CRT, but the compiler still lowers struct copies and
// comparisons to these libc names, and the headers' inline helpers
// (IsEqualGUID) call memcmp. Each shim is one translation unit, so these are
// one definition per DLL.
extern "C" void* memset(void* d,int c,size_t n) {
    auto p=(unsigned char*)d;while(n--)*p++=(unsigned char)c;return d;
}
extern "C" void* memcpy(void* d,const void* s,size_t n) {
    auto a=(unsigned char*)d;auto b=(const unsigned char*)s;while(n--)*a++=*b++;return d;
}
extern "C" int memcmp(const void* a,const void* b,size_t n) {
    auto x=(const unsigned char*)a;auto y=(const unsigned char*)b;
    while(n--){if(*x!=*y)return *x<*y?-1:1;++x;++y;}return 0;
}
// Only the placement form of operator new: wrappers are constructed in Play's
// heap and freed with HeapFree, so no CRT allocation is ever involved. It must
// be at namespace scope: the language forbids a namespace-scope operator new.
inline void* operator new(size_t,void* p) noexcept { return p; }
inline void operator delete(void*,void*) noexcept {}
namespace d3dshim {
inline void* allocate(size_t bytes) { return HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,bytes); }
inline void release(void* memory) { if(memory)HeapFree(GetProcessHeap(),0,memory); }
// Play routes a guest's OutputDebugStringA to the launch log, so the shims
// report through the same channel as the guest.
inline void note(const char* text) { OutputDebugStringA(text); }
// Games retry failed calls every frame; report each distinct line once. Only
// string literals may be passed: the table keeps the pointer.
inline void noteOnce(const char* text) {
    static const char* seen[32]; static unsigned count;
    for(unsigned i=0;i<count;++i) {
        const char* a=seen[i];const char* b=text;
        while(*a&&*a==*b){++a;++b;}
        if(!*a&&!*b)return;
    }
    if(count<32)seen[count++]=text;
    note(text);
}
// Bounded formatter; the shims have no stdio.
struct Line {
    char text[256]; unsigned used=0;
    explicit Line(const char* prefix) { add(prefix); }
    void add(const char* s) { while(*s&&used+1<sizeof text)text[used++]=*s++;text[used]=0; }
    void dec(unsigned long long value) {
        char digits[24];unsigned n=0;
        do{digits[n++]=char('0'+value%10);value/=10;}while(value);
        while(n){char one[2]={digits[--n],0};add(one);}
    }
    void hex(unsigned long long value) {
        char digits[20];unsigned n=0;
        do{digits[n++]="0123456789abcdef"[value&15];value>>=4;}while(value);
        add("0x");while(n){char one[2]={digits[--n],0};add(one);}
    }
    void emit() { note(text); }
};
// One D3D11 device and its immediate context, created through Play's route.
struct Host {
    ID3D11Device* device=nullptr;
    ID3D11DeviceContext* context=nullptr;
    D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_10_0;
    HRESULT create(IDXGIAdapter* adapter,D3D_DRIVER_TYPE type,const D3D_FEATURE_LEVEL* levels,
                   UINT count,UINT flags) {
        HRESULT hr=hooks(adapter,type,levels,count,flags);
        // Windows 7's d3d11 rejects 11_1 in the list; retry without it as the
        // runtime documents, so a host that predates it still answers.
        if(hr==E_INVALIDARG&&levels&&count) {
            D3D_FEATURE_LEVEL reduced[8];UINT n=0;
            for(UINT i=0;i<count&&n<8;++i)if(levels[i]!=D3D_FEATURE_LEVEL_11_1)reduced[n++]=levels[i];
            if(n&&n<count)hr=hooks(adapter,type,reduced,n,flags);
        }
        if(FAILED(hr)) {
            Line line("D3D shim: D3D11CreateDevice failed hr=");line.hex((unsigned long)hr);line.emit();
            device=nullptr;context=nullptr;return hr;
        }
        {Line line("D3D shim: D3D11 device feature level=");line.hex(unsigned(level));line.emit();}
        return S_OK;
    }
    HRESULT hooks(IDXGIAdapter* adapter,D3D_DRIVER_TYPE type,const D3D_FEATURE_LEVEL* levels,
                  UINT count,UINT flags) {
        static const D3D_FEATURE_LEVEL fallback[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0,D3D_FEATURE_LEVEL_9_3};
        if(!levels||!count){levels=fallback;count=sizeof fallback/sizeof fallback[0];}
        return D3D11CreateDevice(adapter,type,nullptr,flags,levels,count,D3D11_SDK_VERSION,
            &device,&level,&context);
    }
    void destroy() {
        if(context){context->Release();context=nullptr;}
        if(device){device->Release();device=nullptr;}
    }
};
// D3D11's immediate context keeps a reference on every object bound to it, so
// a guest may release what it bound and the context still draws with it. Wine's
// d3d11 does not take that reference, so a guest following the documented
// pattern would leave the context pointing at freed memory. The shims hold the
// reference themselves, per binding site, and drop it when the slot is rebound.
template<unsigned N> struct Held {
    IUnknown* objects[N]{};
    void set(unsigned index,IUnknown* object) {
        if(index>=N||objects[index]==object)return;
        if(objects[index])objects[index]->Release();
        objects[index]=object;
        if(object)object->AddRef();
    }
    void setRange(unsigned start,unsigned count,IUnknown* const* list) {
        for(unsigned i=0;i<count;++i)set(start+i,list?list[i]:nullptr);
    }
    void clear() { for(unsigned i=0;i<N;++i)set(i,nullptr); }
};
// Play's DXGI factory proxy: guest HWND -> CoreWindow, virtual console output.
inline HRESULT factory(IDXGIFactory** out) {
    if(!out)return E_POINTER;
    *out=nullptr;
    return CreateDXGIFactory1(__uuidof(IDXGIFactory),reinterpret_cast<void**>(out));
}
// Legacy DXGI_SWAP_CHAIN_DESC -> the CoreWindow adapter's CreateSwapChain.
// The adapter refuses non-windowed chains and multisampled buffers (it has no
// resolve layer), so those are reported instead of silently changed.
inline HRESULT createSwapChain(IDXGIFactory* factory,ID3D11Device* device,
        const DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** out) {
    if(!factory||!device||!desc||!out)return E_POINTER;
    *out=nullptr;
    if(!desc->Windowed) {
        noteOnce("D3D shim: exclusive fullscreen refused; the console presents through the CoreWindow");
        return DXGI_ERROR_UNSUPPORTED;
    }
    if(desc->SampleDesc.Count!=1) {
        noteOnce("D3D shim: multisampled swapchain refused; the CoreWindow adapter has no resolve layer");
        return DXGI_ERROR_UNSUPPORTED;
    }
    DXGI_SWAP_CHAIN_DESC copy=*desc;
    if(!copy.BufferCount)copy.BufferCount=1;
    copy.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;   // the adapter converts to flip presentation
    copy.Flags=0;
    return factory->CreateSwapChain(device,&copy,out);
}
// Every wrapper a shim hands to a guest answers this private IID with its
// bookkeeping object, so the shim can recover the D3D11 object behind an
// interface it was given without RTTI (the shims are built -fno-rtti). Like
// corewindow_dxgi::realAdapterId, this is an internal contract; unlike a real
// interface it does NOT add a reference.
static const GUID baseId={0x4a1d9e77,0x2c3b,0x4f01,{0x9d,0x5e,0x11,0x7a,0x3c,0x00,0x8b,0x21}};
// Deliberately no virtual functions: a COM object's vtable must begin with the
// interface's own QueryInterface/AddRef/Release, so Base must not contribute
// slots. Release therefore calls the concrete type's destructor by name.
struct Base {
    LONG refs=1;
    void* self=nullptr;                 // the interface pointer the guest holds
    ID3D11DeviceChild* inner=nullptr;   // the D3D11 object behind it
    IUnknown* owner=nullptr;            // the shim's device, held for its lifetime
    ID3D11DeviceContext* context=nullptr;   // borrowed from `owner`
    // Interfaces this wrapper answers itself; anything else is forwarded to
    // `forward` (the D3D11 object), as Wine's d3d10 objects do.
    const IID* const* interfaceIds=nullptr;
    unsigned interfaceCount=0;
    IUnknown* forward=nullptr;
    // Destroys the complete wrapper. Release cannot name the concrete type: the
    // shared bases (Child, Resource, ...) are templates over the interface, so
    // `this->~I()` there is the interface's trivial destructor and would skip
    // ~Base, leaking the reference and leaving a dangling private-data pointer
    // on the D3D11 object. create<T> stores the right one.
    void (*dispose)(Base*)=nullptr;
    // A wrapper owns one reference on the object it proxies, and leaves no
    // pointer to itself behind on that object.
    ~Base() {
        if(inner) {
            inner->SetPrivateData(baseId,0,nullptr);
            inner->Release();
        }
        if(owner)owner->Release();
    }
};
template<class T> inline void destroy(T* object) {
    object->~T();
    release(const_cast<void*>(static_cast<const void*>(object)));
}
// Constructs a wrapper in Play's heap. Zeroed storage first, so every member
// starts at its zero value and the constructor only sets what it means to.
template<class T> inline T* create() {
    void* memory=allocate(sizeof(T));
    if(!memory)return nullptr;
    T* object=new(memory) T();
    // Objects that are not wrappers (the reflection helpers) have no Base and
    // are destroyed by name by their owner.
    if constexpr(__is_base_of(Base,T)) {
        object->dispose=[](Base* base){ destroy(static_cast<T*>(base)); };
    }
    return object;
}
inline Base* baseOf(IUnknown* object) {
    if(!object)return nullptr;
    Base* found=nullptr;
    return SUCCEEDED(object->QueryInterface(baseId,reinterpret_cast<void**>(&found)))?found:nullptr;
}
inline ID3D11DeviceChild* innerOf(IUnknown* object) {
    Base* found=baseOf(object);
    return found?found->inner:nullptr;
}
// D3D11 objects carry their wrapper as private data, so a Get* call can hand
// back the same D3D10 object the guest already holds instead of a duplicate.
inline void attach(ID3D11DeviceChild* object,Base* wrapper) {
    if(object)object->SetPrivateData(baseId,sizeof wrapper,&wrapper);
}
inline Base* wrapperOf(ID3D11DeviceChild* object) {
    if(!object)return nullptr;
    Base* found=nullptr;UINT size=sizeof found;
    return SUCCEEDED(object->GetPrivateData(baseId,&size,&found))?found:nullptr;
}
// The guest-visible interface of the wrapper behind a D3D11 object. `self` is
// that interface pointer, so no adjustment is needed. The caller still owns
// the reference it must hand out (AddRef it).
template<class T> inline T* wrap(ID3D11DeviceChild* object) {
    Base* found=wrapperOf(object);
    return found?reinterpret_cast<T*>(found->self):nullptr;
}
}
// The two IUnknown entries every shim wrapper shares. `refs`, `self` and the
// interface list come from d3dshim::Base. Release destroys through `dispose`,
// which create<T> set to the complete type's destructor.
#define D3DSHIM_REF \
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); } \
    ULONG STDMETHODCALLTYPE Release() override { \
        ULONG left=InterlockedDecrement(&refs); \
        if(!left) { if(dispose)dispose(this); } \
        return left; }
#define D3DSHIM_QI \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override { \
        if(!out)return E_POINTER; \
        *out=nullptr; \
        if(IsEqualGUID(id,d3dshim::baseId)){*out=static_cast<d3dshim::Base*>(this);return S_OK;} \
        for(unsigned i=0;i<interfaceCount;++i) \
            if(IsEqualGUID(id,*interfaceIds[i])){*out=static_cast<IUnknown*>(this);AddRef();return S_OK;} \
        return forward?forward->QueryInterface(id,out):E_NOINTERFACE; \
    }
// Names the interfaces a wrapper answers; call in its constructor.
#define D3DSHIM_IDS(...) \
    do { static const IID* const list[]={__VA_ARGS__}; \
         interfaceIds=list; interfaceCount=sizeof list/sizeof list[0]; } while(0)
