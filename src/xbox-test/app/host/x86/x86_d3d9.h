// SPDX-License-Identifier: GPL-3.0-or-later
// PE32 D3D9 imports and COM proxy marshalling onto Play's in-process D3D9 shim.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include "x86com.h"
#if defined(_WIN32)
#include <windows.h>
#pragma push_macro("WINAPI_FAMILY")
#undef WINAPI_FAMILY
#define WINAPI_FAMILY WINAPI_FAMILY_DESKTOP_APP
#include <d3d9.h>
#pragma pop_macro("WINAPI_FAMILY")
namespace x86 {
inline constexpr uint32_t D3D9_E_NOTIMPL = 0x80004001u;
inline constexpr uint32_t D3D9_GUEST_BASE = 0x7D000000u;
inline constexpr uint32_t D3D9_GUEST_SIZE = 0x01000000u;
inline constexpr GUID D3D9_IID_TEXTURE = {0x85c31227,0x3de5,0x4f00,{0x9b,0x3a,0xf1,0x1a,0xc3,0x8c,0x18,0xb5}};

inline void d3d9_gap(Host& h, Proxy& p, Cpu&, const char* method) {
    char line[192]; snprintf(line, sizeof line, "x86 D3D9 gap: %s::%s", p.iface, method);
    if (h.tee) h.tee(line);
}
template<size_t N> inline void d3d9_register_gaps(ComSpace& s, Host& h, const char* iface, const unsigned (&arities)[N]) {
    for (unsigned slot = 3; slot < N; ++slot) {
        auto registered = s.ifaces.at(iface).methods.find(slot);
        if (registered != s.ifaces.at(iface).methods.end()) {
            if (registered->second.argc != arities[slot]) throw std::logic_error("D3D9 COM slot arity mismatch");
            continue;
        }
        s.method(iface, slot, arities[slot], [&h, slot](Proxy& p, Cpu& c) -> uint32_t {
            char name[24]; snprintf(name, sizeof name, "slot %u", slot);
            d3d9_gap(h, p, c, name);
            return D3D9_E_NOTIMPL;
        });
    }
}
inline uint32_t d3d9_wrap(Cpu& c, Proxy& p, const char* iface, IUnknown* object) {
    if (!object) return 0;
    auto addref = [object]() { object->AddRef(); };
    auto release = [object]() { object->Release(); };
    uint32_t guest = p.space->create(c, iface, object, addref, release);
    object->Release(); // transfer the returned COM reference into the proxy
    return guest;
}
inline uint32_t d3d9_wrap(ComSpace& space, Cpu& c, const char* iface, IUnknown* object) {
    if (!object) return 0;
    uint32_t guest = space.create(c, iface, object, [object]() { object->AddRef(); }, [object]() { object->Release(); });
    object->Release();
    return guest;
}
inline HRESULT d3d9_present_parameters(Host& h, Cpu& c, uint32_t guest, D3DPRESENT_PARAMETERS& host) {
    if (!guest) return E_POINTER;
    uint32_t v[14]{};
    for (unsigned i = 0; i < 14; ++i) v[i] = c.mem.r32(guest + i * 4);
    host.BackBufferWidth=v[0]; host.BackBufferHeight=v[1]; host.BackBufferFormat=D3DFORMAT(v[2]);
    host.BackBufferCount=v[3]; host.MultiSampleType=D3DMULTISAMPLE_TYPE(v[4]); host.MultiSampleQuality=v[5];
    host.SwapEffect=D3DSWAPEFFECT(v[6]); host.hDeviceWindow=reinterpret_cast<HWND>(h.d3d9_window?h.d3d9_window(v[7]):(v[7]?0:0));
    host.Windowed=v[8]; host.EnableAutoDepthStencil=v[9]; host.AutoDepthStencilFormat=D3DFORMAT(v[10]);
    host.Flags=v[11]; host.FullScreen_RefreshRateInHz=v[12]; host.PresentationInterval=v[13];
    return S_OK;
}
inline void d3d9_write_present_parameters(Cpu& c, uint32_t guest, const D3DPRESENT_PARAMETERS& p) {
    const uint32_t v[14]={p.BackBufferWidth,p.BackBufferHeight,uint32_t(p.BackBufferFormat),p.BackBufferCount,
        uint32_t(p.MultiSampleType),p.MultiSampleQuality,uint32_t(p.SwapEffect),c.mem.r32(guest+28),uint32_t(p.Windowed),uint32_t(p.EnableAutoDepthStencil),uint32_t(p.AutoDepthStencilFormat),p.Flags,
        p.FullScreen_RefreshRateInHz,p.PresentationInterval};
    for (unsigned i=0;i<14;++i)c.mem.w32(guest+i*4,v[i]);
}
template<class T> inline T d3d9_read_struct(Cpu& c,uint32_t address) {
    auto bytes=c.mem.read_bytes(address,sizeof(T));T value{};memcpy(&value,bytes.data(),sizeof(T));return value;
}
inline uint32_t d3d9_guest_buffer(Host& h, Cpu& c, uint32_t size) {
    if (!size || size > D3D9_GUEST_SIZE) throw Fault(FaultKind::AccessViolation,c.eip,h.d3d9_guest_top,"D3D9 guest buffer size");
    uint32_t address=(h.d3d9_guest_top+15u)&~15u;
    uint64_t end=uint64_t(address)+size;
    if(end>D3D9_GUEST_BASE+D3D9_GUEST_SIZE)throw Fault(FaultKind::AccessViolation,c.eip,address,"D3D9 guest buffer window exhausted");
    uint32_t mapped=(uint32_t(end)+0xFFFu)&~0xFFFu;
    uint32_t previous=(h.d3d9_guest_top+0xFFFu)&~0xFFFu;
    if(mapped>previous)c.mem.map(previous,mapped-previous,PROT_R|PROT_W);
    h.d3d9_guest_top=uint32_t(end);
    return address;
}
inline uint32_t d3d9_shader_bytes(Cpu& c,uint32_t address) {
    for(uint32_t i=0;i<(1u<<18);++i)if(c.mem.r32(address+i*4)==0x0000FFFFu)return (i+1)*4;
    throw Fault(FaultKind::Unsupported,c.eip,address,"D3D9 shader bytecode has no END token");
}
inline float d3d9_float_slot(Cpu& c,unsigned slot) {
    uint32_t bits=c.arg(slot);float value;memcpy(&value,&bits,sizeof(value));return value;
}
inline constexpr uint64_t d3d9_up_index_count(D3DPRIMITIVETYPE type,uint32_t primitives) {
    switch(type) {
    case D3DPT_POINTLIST: return primitives;
    case D3DPT_LINELIST: return uint64_t(primitives)*2;
    case D3DPT_LINESTRIP: return primitives?uint64_t(primitives)+1:0;
    case D3DPT_TRIANGLELIST: return uint64_t(primitives)*3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN: return primitives?uint64_t(primitives)+2:0;
    default: return 0;
    }
}
inline void register_d3d9_com(ComSpace& s, Host& h) {
    s.interface_("IDirect3D9",17);
    s.interface_("IDirect3D9Ex",22);
    s.interface_("IDirect3DDevice9",119);
    s.interface_("IDirect3DSurface9",17);
    s.interface_("IDirect3DVertexBuffer9",14);
    s.interface_("IDirect3DIndexBuffer9",14);
    s.interface_("IDirect3DTexture9",22);
    s.interface_("IDirect3DSwapChain9",10);
    s.interface_("IDirect3DStateBlock9",7);
    s.interface_("IDirect3DVertexDeclaration9",5);
    s.interface_("IDirect3DVertexShader9",5);
    s.interface_("IDirect3DPixelShader9",5);
    auto method=[&](const char* iface,unsigned slot,unsigned argc,Marshaller fn){
        if(s.ifaces.at(iface).methods.count(slot))throw std::logic_error("duplicate D3D9 COM slot registration");
        s.method(iface,slot,argc,std::move(fn));
    };
    auto d3d9=[&](Proxy& p)->IDirect3D9* { return static_cast<IDirect3D9*>(p.host); };
    auto dev=[&](Proxy& p)->IDirect3DDevice9* { return static_cast<IDirect3DDevice9*>(p.host); };
    // IDirect3D9 and the shared IDirect3D9Ex prefix. Pointer-bearing output
    // structures are copied through guest Memory; no PE32 address escapes.
    method("IDirect3D9",4,1,[d3d9](Proxy& p,Cpu&){return d3d9(p)->GetAdapterCount();});
    method("IDirect3D9Ex",4,1,[d3d9](Proxy& p,Cpu&){return d3d9(p)->GetAdapterCount();});
    method("IDirect3D9",5,4,[d3d9](Proxy& p,Cpu& c)->uint32_t { D3DADAPTER_IDENTIFIER9 x{}; HRESULT hr=d3d9(p)->GetAdapterIdentifier(c.arg(1),c.arg(2),&x); if(SUCCEEDED(hr)){uint32_t a=c.arg(3);c.mem.write_bytes(a,&x,sizeof(x));}return uint32_t(hr);});
    method("IDirect3D9Ex",5,4,[d3d9](Proxy& p,Cpu& c)->uint32_t { D3DADAPTER_IDENTIFIER9 x{};HRESULT hr=d3d9(p)->GetAdapterIdentifier(c.arg(1),c.arg(2),&x);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(3),&x,sizeof(x));return uint32_t(hr);});
    method("IDirect3D9",6,3,[d3d9](Proxy& p,Cpu& c){return d3d9(p)->GetAdapterModeCount(c.arg(1),D3DFORMAT(c.arg(2)));});
    method("IDirect3D9Ex",6,3,[d3d9](Proxy& p,Cpu& c){return d3d9(p)->GetAdapterModeCount(c.arg(1),D3DFORMAT(c.arg(2)));});
    method("IDirect3D9",7,5,[d3d9](Proxy& p,Cpu& c)->uint32_t { D3DDISPLAYMODE m{};HRESULT hr=d3d9(p)->EnumAdapterModes(c.arg(1),D3DFORMAT(c.arg(2)),c.arg(3),&m);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(4),&m,sizeof(m));return uint32_t(hr);});
    method("IDirect3D9Ex",7,5,[d3d9](Proxy& p,Cpu& c)->uint32_t { D3DDISPLAYMODE m{};HRESULT hr=d3d9(p)->EnumAdapterModes(c.arg(1),D3DFORMAT(c.arg(2)),c.arg(3),&m);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(4),&m,sizeof(m));return uint32_t(hr);});
    method("IDirect3D9",8,3,[d3d9](Proxy& p,Cpu& c)->uint32_t {D3DDISPLAYMODE m{};HRESULT hr=d3d9(p)->GetAdapterDisplayMode(c.arg(1),&m);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(2),&m,sizeof(m));return uint32_t(hr);});
    method("IDirect3D9Ex",8,3,[d3d9](Proxy& p,Cpu& c)->uint32_t {D3DDISPLAYMODE m{};HRESULT hr=d3d9(p)->GetAdapterDisplayMode(c.arg(1),&m);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(2),&m,sizeof(m));return uint32_t(hr);});
    method("IDirect3D9",9,6,[d3d9](Proxy& p,Cpu& c){return uint32_t(d3d9(p)->CheckDeviceType(c.arg(1),D3DDEVTYPE(c.arg(2)),D3DFORMAT(c.arg(3)),D3DFORMAT(c.arg(4)),c.arg(5)));});
    method("IDirect3D9Ex",9,6,[d3d9](Proxy& p,Cpu& c){return uint32_t(d3d9(p)->CheckDeviceType(c.arg(1),D3DDEVTYPE(c.arg(2)),D3DFORMAT(c.arg(3)),D3DFORMAT(c.arg(4)),c.arg(5)));});
    method("IDirect3D9",10,7,[d3d9,&h](Proxy& p,Cpu& c){if(h.route_trace)h.route_trace("D3D9 CheckDeviceFormat");return uint32_t(d3d9(p)->CheckDeviceFormat(c.arg(1),D3DDEVTYPE(c.arg(2)),D3DFORMAT(c.arg(3)),c.arg(4),D3DRESOURCETYPE(c.arg(5)),D3DFORMAT(c.arg(6))));});
    method("IDirect3D9Ex",10,7,[d3d9](Proxy& p,Cpu& c){return uint32_t(d3d9(p)->CheckDeviceFormat(c.arg(1),D3DDEVTYPE(c.arg(2)),D3DFORMAT(c.arg(3)),c.arg(4),D3DRESOURCETYPE(c.arg(5)),D3DFORMAT(c.arg(6))));});
    auto check_multisample=[&](const char* iface){method(iface,11,7,[d3d9](Proxy& p,Cpu& c)->uint32_t {DWORD quality=0;HRESULT hr=d3d9(p)->CheckDeviceMultiSampleType(c.arg(1),D3DDEVTYPE(c.arg(2)),D3DFORMAT(c.arg(3)),c.arg(4),D3DMULTISAMPLE_TYPE(c.arg(5)),c.arg(6)?&quality:nullptr);if(SUCCEEDED(hr)&&c.arg(6))c.mem.w32(c.arg(6),quality);return uint32_t(hr);});method(iface,12,6,[d3d9](Proxy& p,Cpu& c){return uint32_t(d3d9(p)->CheckDepthStencilMatch(c.arg(1),D3DDEVTYPE(c.arg(2)),D3DFORMAT(c.arg(3)),D3DFORMAT(c.arg(4)),D3DFORMAT(c.arg(5))));});};
    check_multisample("IDirect3D9");check_multisample("IDirect3D9Ex");
    method("IDirect3D9",14,4,[d3d9](Proxy& p,Cpu& c)->uint32_t {D3DCAPS9 x{};HRESULT hr=d3d9(p)->GetDeviceCaps(c.arg(1),D3DDEVTYPE(c.arg(2)),&x);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(3),&x,sizeof(x));return uint32_t(hr);});
    method("IDirect3D9Ex",14,4,[d3d9](Proxy& p,Cpu& c)->uint32_t {D3DCAPS9 x{};HRESULT hr=d3d9(p)->GetDeviceCaps(c.arg(1),D3DDEVTYPE(c.arg(2)),&x);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(3),&x,sizeof(x));return uint32_t(hr);});
    auto create_device=[&](const char* iface) { method(iface,16,7,[d3d9,&s,&h](Proxy& p,Cpu& c)->uint32_t {
        if(!c.arg(6))return E_POINTER;
        D3DPRESENT_PARAMETERS pp{};if(FAILED(d3d9_present_parameters(h,c,c.arg(5),pp)))return E_POINTER;
        IDirect3DDevice9* object=nullptr;HWND focus=reinterpret_cast<HWND>(h.d3d9_window?h.d3d9_window(c.arg(3)):(c.arg(3)?0:0));HRESULT hr=d3d9(p)->CreateDevice(c.arg(1),D3DDEVTYPE(c.arg(2)),focus,c.arg(4),&pp,&object);
        d3d9_write_present_parameters(c,c.arg(5),pp);
        if(SUCCEEDED(hr)&&object){object->AddRef();s.d3d9_focus_windows[object]={c.arg(3),[object](){object->Release();}};uint32_t guest=d3d9_wrap(s,c,"IDirect3DDevice9",object);s.by_guest(guest).d3d9_focus_window=c.arg(3);c.mem.w32(c.arg(6),guest);}else if(c.arg(6))c.mem.w32(c.arg(6),0);
        return uint32_t(hr);
    });};
    create_device("IDirect3D9");create_device("IDirect3D9Ex");

    // IDirect3DDevice9 methods. Slots follow the order in d3d9.h; argc counts
    // x86 stdcall DWORD slots, including both halves of any 64-bit by-value arg.
    method("IDirect3DDevice9",4,1,[dev](Proxy& p,Cpu&){return dev(p)->GetAvailableTextureMem();});
    method("IDirect3DDevice9",5,1,[dev](Proxy& p,Cpu&){return uint32_t(dev(p)->EvictManagedResources());});
    method("IDirect3DDevice9",6,2,[dev,&s,&h](Proxy& p,Cpu& c)->uint32_t {if(h.route_trace)h.route_trace("D3D9 Device GetDirect3D");if(!c.arg(1))return E_POINTER;IDirect3D9* x=nullptr;HRESULT hr=dev(p)->GetDirect3D(&x);if(SUCCEEDED(hr))c.mem.w32(c.arg(1),d3d9_wrap(s,c,"IDirect3D9",x));else c.mem.w32(c.arg(1),0);return uint32_t(hr);});
    method("IDirect3DDevice9",7,2,[dev,&h](Proxy& p,Cpu& c)->uint32_t {if(h.route_trace)h.route_trace("D3D9 Device GetDeviceCaps");if(!c.arg(1))return E_POINTER;D3DCAPS9 x{};HRESULT hr=dev(p)->GetDeviceCaps(&x);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(1),&x,sizeof(x));return uint32_t(hr);});
    method("IDirect3DDevice9",8,3,[dev,&h](Proxy& p,Cpu& c)->uint32_t {if(h.route_trace)h.route_trace("D3D9 Device GetDisplayMode");if(!c.arg(2))return E_POINTER;D3DDISPLAYMODE x{};HRESULT hr=dev(p)->GetDisplayMode(c.arg(1),&x);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(2),&x,sizeof(x));return uint32_t(hr);});
    method("IDirect3DDevice9",9,2,[dev,&h](Proxy& p,Cpu& c)->uint32_t {if(h.route_trace)h.route_trace("D3D9 Device GetCreationParameters");if(!c.arg(1))return E_POINTER;D3DDEVICE_CREATION_PARAMETERS x{};HRESULT hr=dev(p)->GetCreationParameters(&x);if(SUCCEEDED(hr)){uint32_t a=c.arg(1);c.mem.w32(a,x.AdapterOrdinal);c.mem.w32(a+4,uint32_t(x.DeviceType));c.mem.w32(a+8,p.d3d9_focus_window);c.mem.w32(a+12,x.BehaviorFlags);}return uint32_t(hr);});
    method("IDirect3DDevice9",10,4,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(3))return E_POINTER;auto* bitmap=static_cast<IDirect3DSurface9*>(s.by_guest(c.arg(3)).host);return uint32_t(dev(p)->SetCursorProperties(c.arg(1),c.arg(2),bitmap));});
    method("IDirect3DDevice9",11,4,[dev](Proxy& p,Cpu& c)->uint32_t {dev(p)->SetCursorPosition(int32_t(c.arg(1)),int32_t(c.arg(2)),c.arg(3));return 0;});
    method("IDirect3DDevice9",12,2,[dev](Proxy& p,Cpu& c){return uint32_t(dev(p)->ShowCursor(c.arg(1)!=0));});
    method("IDirect3DDevice9",14,3,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;IDirect3DSwapChain9* x=nullptr;HRESULT hr=dev(p)->GetSwapChain(c.arg(1),&x);if(SUCCEEDED(hr))c.mem.w32(c.arg(2),d3d9_wrap(s,c,"IDirect3DSwapChain9",x));else c.mem.w32(c.arg(2),0);return uint32_t(hr);});
    method("IDirect3DDevice9",21,4,[dev](Proxy& p,Cpu& c){D3DGAMMARAMP ramp{};if(c.arg(3)){auto b=c.mem.read_bytes(c.arg(3),sizeof(ramp));memcpy(&ramp,b.data(),sizeof(ramp));}dev(p)->SetGammaRamp(c.arg(1),c.arg(2),c.arg(3)?&ramp:nullptr);return 0u;});
    method("IDirect3DDevice9",22,3,[dev](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;D3DGAMMARAMP ramp{};dev(p)->GetGammaRamp(c.arg(1),&ramp);c.mem.write_bytes(c.arg(2),&ramp,sizeof(ramp));return 0;});
    method("IDirect3DDevice9",23,9,[dev,&s,&h](Proxy& p,Cpu& c)->uint32_t {if(h.route_trace)h.route_trace("D3D9 Device CreateTexture out=0x"+std::to_string(c.arg(7))+" format=0x"+std::to_string(c.arg(5)));if(!c.arg(7))return E_POINTER;IDirect3DTexture9* x=nullptr;HRESULT hr=dev(p)->CreateTexture(c.arg(1),c.arg(2),c.arg(3),c.arg(4),D3DFORMAT(c.arg(5)),D3DPOOL(c.arg(6)),&x,nullptr);if(h.route_trace)h.route_trace("D3D9 Device CreateTexture hr=0x"+std::to_string(uint32_t(hr)));if(SUCCEEDED(hr))c.mem.w32(c.arg(7),d3d9_wrap(s,c,"IDirect3DTexture9",x));else c.mem.w32(c.arg(7),0);return uint32_t(hr);});
    method("IDirect3DDevice9",27,7,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(5))return E_POINTER;IDirect3DIndexBuffer9* x=nullptr;HRESULT hr=dev(p)->CreateIndexBuffer(c.arg(1),c.arg(2),D3DFORMAT(c.arg(3)),D3DPOOL(c.arg(4)),&x,nullptr);if(SUCCEEDED(hr))c.mem.w32(c.arg(5),d3d9_wrap(s,c,"IDirect3DIndexBuffer9",x));else c.mem.w32(c.arg(5),0);return uint32_t(hr);});
    method("IDirect3DDevice9",28,9,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(7))return E_POINTER;IDirect3DSurface9* x=nullptr;HRESULT hr=dev(p)->CreateRenderTarget(c.arg(1),c.arg(2),D3DFORMAT(c.arg(3)),D3DMULTISAMPLE_TYPE(c.arg(4)),c.arg(5),c.arg(6),&x,nullptr);if(SUCCEEDED(hr))c.mem.w32(c.arg(7),d3d9_wrap(s,c,"IDirect3DSurface9",x));else c.mem.w32(c.arg(7),0);return uint32_t(hr);});
    method("IDirect3DDevice9",29,9,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(7))return E_POINTER;IDirect3DSurface9* x=nullptr;HRESULT hr=dev(p)->CreateDepthStencilSurface(c.arg(1),c.arg(2),D3DFORMAT(c.arg(3)),D3DMULTISAMPLE_TYPE(c.arg(4)),c.arg(5),c.arg(6),&x,nullptr);if(SUCCEEDED(hr))c.mem.w32(c.arg(7),d3d9_wrap(s,c,"IDirect3DSurface9",x));else c.mem.w32(c.arg(7),0);return uint32_t(hr);});
    method("IDirect3DDevice9",30,5,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1)||!c.arg(3))return E_POINTER;auto* src=static_cast<IDirect3DSurface9*>(s.by_guest(c.arg(1)).host);auto* dst=static_cast<IDirect3DSurface9*>(s.by_guest(c.arg(3)).host);RECT rect{};RECT* rp=nullptr;POINT point{};POINT* pp=nullptr;if(c.arg(2)){rect=d3d9_read_struct<RECT>(c,c.arg(2));rp=&rect;}if(c.arg(4)){point=d3d9_read_struct<POINT>(c,c.arg(4));pp=&point;}return uint32_t(dev(p)->UpdateSurface(src,rp,dst,pp));});
    method("IDirect3DDevice9",31,3,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1)||!c.arg(2))return E_POINTER;return uint32_t(dev(p)->UpdateTexture(static_cast<IDirect3DBaseTexture9*>(s.by_guest(c.arg(1)).host),static_cast<IDirect3DBaseTexture9*>(s.by_guest(c.arg(2)).host)));});
    method("IDirect3DDevice9",34,6,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1)||!c.arg(3))return E_POINTER;RECT a{},b{};RECT* ap=nullptr;RECT* bp=nullptr;if(c.arg(2)){a=d3d9_read_struct<RECT>(c,c.arg(2));ap=&a;}if(c.arg(4)){b=d3d9_read_struct<RECT>(c,c.arg(4));bp=&b;}return uint32_t(dev(p)->StretchRect(static_cast<IDirect3DSurface9*>(s.by_guest(c.arg(1)).host),ap,static_cast<IDirect3DSurface9*>(s.by_guest(c.arg(3)).host),bp,D3DTEXTUREFILTERTYPE(c.arg(5))));});
    method("IDirect3DDevice9",35,4,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;RECT r{},*rp=nullptr;if(c.arg(2)){r=d3d9_read_struct<RECT>(c,c.arg(2));rp=&r;}return uint32_t(dev(p)->ColorFill(static_cast<IDirect3DSurface9*>(s.by_guest(c.arg(1)).host),rp,D3DCOLOR(c.arg(3))));});
    method("IDirect3DDevice9",37,3,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(c.arg(2)==0)return uint32_t(dev(p)->SetRenderTarget(c.arg(1),nullptr));return uint32_t(dev(p)->SetRenderTarget(c.arg(1),static_cast<IDirect3DSurface9*>(s.by_guest(c.arg(2)).host)));});
    method("IDirect3DDevice9",38,3,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;IDirect3DSurface9* x=nullptr;HRESULT hr=dev(p)->GetRenderTarget(c.arg(1),&x);if(SUCCEEDED(hr))c.mem.w32(c.arg(2),d3d9_wrap(s,c,"IDirect3DSurface9",x));else c.mem.w32(c.arg(2),0);return uint32_t(hr);});
    method("IDirect3DDevice9",39,2,[dev,&s](Proxy& p,Cpu& c)->uint32_t {auto* x=c.arg(1)?static_cast<IDirect3DSurface9*>(s.by_guest(c.arg(1)).host):nullptr;return uint32_t(dev(p)->SetDepthStencilSurface(x));});
    method("IDirect3DDevice9",45,3,[dev](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;D3DMATRIX x{};HRESULT hr=dev(p)->GetTransform(D3DTRANSFORMSTATETYPE(c.arg(1)),&x);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(2),&x,sizeof(x));return uint32_t(hr);});
    method("IDirect3DDevice9",46,3,[dev](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;D3DMATRIX x=d3d9_read_struct<D3DMATRIX>(c,c.arg(2));return uint32_t(dev(p)->MultiplyTransform(D3DTRANSFORMSTATETYPE(c.arg(1)),&x));});
    method("IDirect3DDevice9",49,2,[dev](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;D3DMATERIAL9 x=d3d9_read_struct<D3DMATERIAL9>(c,c.arg(1));return uint32_t(dev(p)->SetMaterial(&x));});
    method("IDirect3DDevice9",50,2,[dev](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;D3DMATERIAL9 x{};HRESULT hr=dev(p)->GetMaterial(&x);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(1),&x,sizeof(x));return uint32_t(hr);});
    method("IDirect3DDevice9",51,3,[dev](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;D3DLIGHT9 x=d3d9_read_struct<D3DLIGHT9>(c,c.arg(2));return uint32_t(dev(p)->SetLight(c.arg(1),&x));});
    method("IDirect3DDevice9",52,3,[dev](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;D3DLIGHT9 x{};HRESULT hr=dev(p)->GetLight(c.arg(1),&x);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(2),&x,sizeof(x));return uint32_t(hr);});
    method("IDirect3DDevice9",53,3,[dev](Proxy& p,Cpu& c){return uint32_t(dev(p)->LightEnable(c.arg(1),c.arg(2)));});
    method("IDirect3DDevice9",54,3,[dev](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;BOOL x=FALSE;HRESULT hr=dev(p)->GetLightEnable(c.arg(1),&x);if(SUCCEEDED(hr))c.mem.w32(c.arg(2),x);return uint32_t(hr);});
    method("IDirect3DDevice9",59,3,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;IDirect3DStateBlock9* x=nullptr;HRESULT hr=dev(p)->CreateStateBlock(D3DSTATEBLOCKTYPE(c.arg(1)),&x);if(SUCCEEDED(hr))c.mem.w32(c.arg(2),d3d9_wrap(s,c,"IDirect3DStateBlock9",x));else c.mem.w32(c.arg(2),0);return uint32_t(hr);});
    method("IDirect3DDevice9",60,1,[dev](Proxy& p,Cpu&){return uint32_t(dev(p)->BeginStateBlock());});
    method("IDirect3DDevice9",61,2,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;IDirect3DStateBlock9* x=nullptr;HRESULT hr=dev(p)->EndStateBlock(&x);if(SUCCEEDED(hr))c.mem.w32(c.arg(1),d3d9_wrap(s,c,"IDirect3DStateBlock9",x));else c.mem.w32(c.arg(1),0);return uint32_t(hr);});
    method("IDirect3DDevice9",64,3,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;IDirect3DBaseTexture9* x=nullptr;HRESULT hr=dev(p)->GetTexture(c.arg(1),&x);if(SUCCEEDED(hr))c.mem.w32(c.arg(2),d3d9_wrap(s,c,"IDirect3DTexture9",x));else c.mem.w32(c.arg(2),0);return uint32_t(hr);});
    method("IDirect3DDevice9",65,3,[dev,&s](Proxy& p,Cpu& c)->uint32_t {auto* x=c.arg(2)?static_cast<IDirect3DBaseTexture9*>(s.by_guest(c.arg(2)).host):nullptr;return uint32_t(dev(p)->SetTexture(c.arg(1),x));});
    method("IDirect3DDevice9",66,4,[dev](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(3))return E_POINTER;DWORD x=0;HRESULT hr=dev(p)->GetTextureStageState(c.arg(1),D3DTEXTURESTAGESTATETYPE(c.arg(2)),&x);if(SUCCEEDED(hr))c.mem.w32(c.arg(3),x);return uint32_t(hr);});
    method("IDirect3DDevice9",67,4,[dev](Proxy& p,Cpu& c){return uint32_t(dev(p)->SetTextureStageState(c.arg(1),D3DTEXTURESTAGESTATETYPE(c.arg(2)),c.arg(3)));});
    method("IDirect3DDevice9",68,4,[dev](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(3))return E_POINTER;DWORD x=0;HRESULT hr=dev(p)->GetSamplerState(c.arg(1),D3DSAMPLERSTATETYPE(c.arg(2)),&x);if(SUCCEEDED(hr))c.mem.w32(c.arg(3),x);return uint32_t(hr);});
    method("IDirect3DDevice9",69,4,[dev](Proxy& p,Cpu& c){return uint32_t(dev(p)->SetSamplerState(c.arg(1),D3DSAMPLERSTATETYPE(c.arg(2)),c.arg(3)));});
    method("IDirect3DDevice9",75,2,[dev](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;RECT x=d3d9_read_struct<RECT>(c,c.arg(1));return uint32_t(dev(p)->SetScissorRect(&x));});
    method("IDirect3DDevice9",76,2,[dev](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;RECT x{};HRESULT hr=dev(p)->GetScissorRect(&x);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(1),&x,sizeof(x));return uint32_t(hr);});
    method("IDirect3DDevice9",79,2,[dev](Proxy& p,Cpu& c){return uint32_t(dev(p)->SetNPatchMode(d3d9_float_slot(c,1)));});
    method("IDirect3DDevice9",82,7,[dev](Proxy& p,Cpu& c){return uint32_t(dev(p)->DrawIndexedPrimitive(D3DPRIMITIVETYPE(c.arg(1)),int32_t(c.arg(2)),c.arg(3),c.arg(4),c.arg(5),c.arg(6)));});
    method("IDirect3DDevice9",84,9,[dev](Proxy& p,Cpu& c)->uint32_t {uint32_t pc=c.arg(4),format=c.arg(6),stride=c.arg(8);uint64_t indices=d3d9_up_index_count(D3DPRIMITIVETYPE(c.arg(1)),pc);uint64_t index_bytes=indices*(format==D3DFMT_INDEX16?2:format==D3DFMT_INDEX32?4:0),vertex_bytes=(uint64_t(c.arg(2))+c.arg(3))*stride;if(!stride||!index_bytes||index_bytes>D3D9_GUEST_SIZE||vertex_bytes>D3D9_GUEST_SIZE)return D3DERR_INVALIDCALL;auto ib=c.mem.read_bytes(c.arg(5),uint32_t(index_bytes));auto vb=c.mem.read_bytes(c.arg(7),uint32_t(vertex_bytes));return uint32_t(dev(p)->DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE(c.arg(1)),c.arg(2),c.arg(3),pc,ib.data(),D3DFORMAT(format),vb.data(),stride));});
    method("IDirect3DDevice9",86,3,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1)||!c.arg(2))return E_POINTER;std::vector<D3DVERTEXELEMENT9> elements;for(unsigned i=0;i<65;++i){auto x=d3d9_read_struct<D3DVERTEXELEMENT9>(c,c.arg(1)+i*sizeof(D3DVERTEXELEMENT9));elements.push_back(x);if(x.Stream==0xFF)break;}if(elements.empty()||elements.back().Stream!=0xFF)return D3DERR_INVALIDCALL;IDirect3DVertexDeclaration9* x=nullptr;HRESULT hr=dev(p)->CreateVertexDeclaration(elements.data(),&x);if(SUCCEEDED(hr))c.mem.w32(c.arg(2),d3d9_wrap(s,c,"IDirect3DVertexDeclaration9",x));else c.mem.w32(c.arg(2),0);return uint32_t(hr);});
    method("IDirect3DDevice9",87,2,[dev,&s](Proxy& p,Cpu& c)->uint32_t {auto* x=c.arg(1)?static_cast<IDirect3DVertexDeclaration9*>(s.by_guest(c.arg(1)).host):nullptr;return uint32_t(dev(p)->SetVertexDeclaration(x));});
    method("IDirect3DDevice9",96,4,[dev](Proxy& p,Cpu& c)->uint32_t {uint32_t n=c.arg(3);if(n>256)return D3DERR_INVALIDCALL;auto b=c.mem.read_bytes(c.arg(2),n*16);return uint32_t(dev(p)->SetVertexShaderConstantI(c.arg(1),reinterpret_cast<const int*>(b.data()),n));});
    method("IDirect3DDevice9",98,4,[dev](Proxy& p,Cpu& c)->uint32_t {uint32_t n=c.arg(3);if(n>256)return D3DERR_INVALIDCALL;auto b=c.mem.read_bytes(c.arg(2),n*4);return uint32_t(dev(p)->SetVertexShaderConstantB(c.arg(1),reinterpret_cast<const BOOL*>(b.data()),n));});
    method("IDirect3DDevice9",104,2,[dev,&s](Proxy& p,Cpu& c)->uint32_t {auto* x=c.arg(1)?static_cast<IDirect3DIndexBuffer9*>(s.by_guest(c.arg(1)).host):nullptr;return uint32_t(dev(p)->SetIndices(x));});
    method("IDirect3DDevice9",109,4,[dev](Proxy& p,Cpu& c)->uint32_t {uint32_t n=c.arg(3);if(n>256)return D3DERR_INVALIDCALL;auto b=c.mem.read_bytes(c.arg(2),n*16);return uint32_t(dev(p)->SetPixelShaderConstantF(c.arg(1),reinterpret_cast<const float*>(b.data()),n));});
    method("IDirect3DDevice9",111,4,[dev](Proxy& p,Cpu& c)->uint32_t {uint32_t n=c.arg(3);if(n>256)return D3DERR_INVALIDCALL;auto b=c.mem.read_bytes(c.arg(2),n*16);return uint32_t(dev(p)->SetPixelShaderConstantI(c.arg(1),reinterpret_cast<const int*>(b.data()),n));});
    method("IDirect3DDevice9",113,4,[dev](Proxy& p,Cpu& c)->uint32_t {uint32_t n=c.arg(3);if(n>256)return D3DERR_INVALIDCALL;auto b=c.mem.read_bytes(c.arg(2),n*4);return uint32_t(dev(p)->SetPixelShaderConstantB(c.arg(1),reinterpret_cast<const BOOL*>(b.data()),n));});
    method("IDirect3DDevice9",3,1,[dev](Proxy& p,Cpu&){return uint32_t(dev(p)->TestCooperativeLevel());});
    method("IDirect3DDevice9",16,2,[dev,&h](Proxy& p,Cpu& c)->uint32_t {D3DPRESENT_PARAMETERS pp{};if(FAILED(d3d9_present_parameters(h,c,c.arg(1),pp)))return E_POINTER;HRESULT hr=dev(p)->Reset(&pp);d3d9_write_present_parameters(c,c.arg(1),pp);return uint32_t(hr);});
    method("IDirect3DDevice9",17,5,[dev,&h](Proxy& p,Cpu& c)->uint32_t {if(c.arg(4)){d3d9_gap(h,p,c,"Present dirty-region");return D3D9_E_NOTIMPL;}RECT src{},dst{};RECT* src_ptr=nullptr;RECT* dst_ptr=nullptr;if(c.arg(1)){src=d3d9_read_struct<RECT>(c,c.arg(1));src_ptr=&src;}if(c.arg(2)){dst=d3d9_read_struct<RECT>(c,c.arg(2));dst_ptr=&dst;}HWND window=reinterpret_cast<HWND>(h.d3d9_window?h.d3d9_window(c.arg(3)):(c.arg(3)?0:0));return uint32_t(dev(p)->Present(src_ptr,dst_ptr,window,nullptr));});
    method("IDirect3DDevice9",18,5,[dev,&s](Proxy& p,Cpu& c)->uint32_t {IDirect3DSurface9* x=nullptr;HRESULT hr=dev(p)->GetBackBuffer(c.arg(1),c.arg(2),D3DBACKBUFFER_TYPE(c.arg(3)),&x);if(SUCCEEDED(hr))c.mem.w32(c.arg(4),d3d9_wrap(s,c,"IDirect3DSurface9",x));else if(c.arg(4))c.mem.w32(c.arg(4),0);return uint32_t(hr);});
    method("IDirect3DDevice9",26,7,[dev,&s](Proxy& p,Cpu& c)->uint32_t {IDirect3DVertexBuffer9* x=nullptr;HRESULT hr=dev(p)->CreateVertexBuffer(c.arg(1),c.arg(2),c.arg(3),D3DPOOL(c.arg(4)),&x,nullptr);if(SUCCEEDED(hr))c.mem.w32(c.arg(5),d3d9_wrap(s,c,"IDirect3DVertexBuffer9",x));else if(c.arg(5))c.mem.w32(c.arg(5),0);return uint32_t(hr);});
    method("IDirect3DDevice9",32,3,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1)||!c.arg(2))return E_POINTER;auto& src=s.by_guest(c.arg(1));auto& dst=s.by_guest(c.arg(2));return uint32_t(dev(p)->GetRenderTargetData(static_cast<IDirect3DSurface9*>(src.host),static_cast<IDirect3DSurface9*>(dst.host)));});
    method("IDirect3DDevice9",36,7,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(5))return E_POINTER;IDirect3DSurface9* x=nullptr;HRESULT hr=dev(p)->CreateOffscreenPlainSurface(c.arg(1),c.arg(2),D3DFORMAT(c.arg(3)),D3DPOOL(c.arg(4)),&x,nullptr);if(SUCCEEDED(hr))c.mem.w32(c.arg(5),d3d9_wrap(s,c,"IDirect3DSurface9",x));else c.mem.w32(c.arg(5),0);return uint32_t(hr);});
    method("IDirect3DDevice9",41,1,[dev](Proxy& p,Cpu&){return uint32_t(dev(p)->BeginScene());});
    method("IDirect3DDevice9",42,1,[dev](Proxy& p,Cpu&){return uint32_t(dev(p)->EndScene());});
    method("IDirect3DDevice9",43,7,[dev](Proxy& p,Cpu& c)->uint32_t {uint32_t count=c.arg(1);if(count>4096)return D3DERR_INVALIDCALL;std::vector<D3DRECT> rects(count);if(count){auto bytes=c.mem.read_bytes(c.arg(2),count*sizeof(D3DRECT));memcpy(rects.data(),bytes.data(),bytes.size());}uint32_t zbits=c.arg(5);float z;memcpy(&z,&zbits,sizeof(z));return uint32_t(dev(p)->Clear(count,rects.empty()?nullptr:rects.data(),c.arg(3),D3DCOLOR(c.arg(4)),z,c.arg(6)));});
    method("IDirect3DDevice9",44,3,[dev](Proxy& p,Cpu& c)->uint32_t {D3DMATRIX m=d3d9_read_struct<D3DMATRIX>(c,c.arg(2));return uint32_t(dev(p)->SetTransform(D3DTRANSFORMSTATETYPE(c.arg(1)),&m));});
    method("IDirect3DDevice9",47,2,[dev](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;D3DVIEWPORT9 v=d3d9_read_struct<D3DVIEWPORT9>(c,c.arg(1));return uint32_t(dev(p)->SetViewport(&v));});
    method("IDirect3DDevice9",57,3,[dev](Proxy& p,Cpu& c){return uint32_t(dev(p)->SetRenderState(D3DRENDERSTATETYPE(c.arg(1)),c.arg(2)));});
    method("IDirect3DDevice9",81,4,[dev](Proxy& p,Cpu& c){return uint32_t(dev(p)->DrawPrimitive(D3DPRIMITIVETYPE(c.arg(1)),c.arg(2),c.arg(3)));});
    method("IDirect3DDevice9",83,5,[dev](Proxy& p,Cpu& c)->uint32_t {uint32_t primitive_count=c.arg(2),stride=c.arg(4);uint64_t vertices=primitive_count;if(c.arg(1)==D3DPT_TRIANGLELIST)vertices*=3;else if(c.arg(1)==D3DPT_TRIANGLESTRIP||c.arg(1)==D3DPT_TRIANGLEFAN)vertices+=2;else if(c.arg(1)==D3DPT_LINELIST)vertices*=2;else if(c.arg(1)==D3DPT_LINESTRIP)vertices+=1;uint64_t bytes=vertices*stride;if(!stride||bytes>D3D9_GUEST_SIZE)return D3DERR_INVALIDCALL;auto data=c.mem.read_bytes(c.arg(3),uint32_t(bytes));return uint32_t(dev(p)->DrawPrimitiveUP(D3DPRIMITIVETYPE(c.arg(1)),primitive_count,data.data(),stride));});
    method("IDirect3DDevice9",89,2,[dev](Proxy& p,Cpu& c){return uint32_t(dev(p)->SetFVF(c.arg(1)));});
    method("IDirect3DDevice9",91,3,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;IDirect3DVertexShader9* x=nullptr;uint32_t n=d3d9_shader_bytes(c,c.arg(1));auto b=c.mem.read_bytes(c.arg(1),n);HRESULT hr=dev(p)->CreateVertexShader(reinterpret_cast<const DWORD*>(b.data()),&x);if(SUCCEEDED(hr)&&c.arg(2))c.mem.w32(c.arg(2),d3d9_wrap(s,c,"IDirect3DVertexShader9",x));return uint32_t(hr);});
    method("IDirect3DDevice9",92,2,[dev,&s](Proxy& p,Cpu& c)->uint32_t {auto* x=c.arg(1)?static_cast<IDirect3DVertexShader9*>(s.by_guest(c.arg(1)).host):nullptr;return uint32_t(dev(p)->SetVertexShader(x));});
    method("IDirect3DDevice9",94,4,[dev](Proxy& p,Cpu& c)->uint32_t {uint32_t n=c.arg(3);if(n>256)return D3DERR_INVALIDCALL;auto bits=c.mem.read_bytes(c.arg(2),n*16);return uint32_t(dev(p)->SetVertexShaderConstantF(c.arg(1),reinterpret_cast<const float*>(bits.data()),n));});
    method("IDirect3DDevice9",100,5,[dev,&s](Proxy& p,Cpu& c)->uint32_t {auto* x=c.arg(2)?static_cast<IDirect3DVertexBuffer9*>(s.by_guest(c.arg(2)).host):nullptr;return uint32_t(dev(p)->SetStreamSource(c.arg(1),x,c.arg(3),c.arg(4)));});
    method("IDirect3DDevice9",106,3,[dev,&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;IDirect3DPixelShader9* x=nullptr;uint32_t n=d3d9_shader_bytes(c,c.arg(1));auto b=c.mem.read_bytes(c.arg(1),n);HRESULT hr=dev(p)->CreatePixelShader(reinterpret_cast<const DWORD*>(b.data()),&x);if(SUCCEEDED(hr)&&c.arg(2))c.mem.w32(c.arg(2),d3d9_wrap(s,c,"IDirect3DPixelShader9",x));return uint32_t(hr);});
    method("IDirect3DDevice9",107,2,[dev,&s](Proxy& p,Cpu& c)->uint32_t {auto* x=c.arg(1)?static_cast<IDirect3DPixelShader9*>(s.by_guest(c.arg(1)).host):nullptr;return uint32_t(dev(p)->SetPixelShader(x));});
    auto register_resource = [&](const char* iface, auto resource_of) {
        method(iface,3,2,[&s,resource_of](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;IDirect3DDevice9* x=nullptr;HRESULT hr=resource_of(static_cast<IUnknown*>(p.host))->GetDevice(&x);if(SUCCEEDED(hr)){bool live=s.has_proxy_for(x);uint32_t guest=d3d9_wrap(s,c,"IDirect3DDevice9",x);auto focus=s.d3d9_focus_windows.find(x);if(focus!=s.d3d9_focus_windows.end()){s.by_guest(guest).d3d9_focus_window=focus->second.focus_window;if(!live&&focus->second.host_release){focus->second.host_release();focus->second.host_release={};}}c.mem.w32(c.arg(1),guest);}else c.mem.w32(c.arg(1),0);return uint32_t(hr);});
        method(iface,4,5,[resource_of](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;DWORD size=c.arg(3);if(size&&!c.arg(2))return E_POINTER;GUID key=d3d9_read_struct<GUID>(c,c.arg(1));auto bytes=size?c.mem.read_bytes(c.arg(2),size):std::vector<uint8_t>{};return uint32_t(resource_of(static_cast<IUnknown*>(p.host))->SetPrivateData(key,size?bytes.data():nullptr,size,c.arg(4)));});
        method(iface,5,4,[resource_of](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1)||!c.arg(3))return E_POINTER;GUID key=d3d9_read_struct<GUID>(c,c.arg(1));DWORD requested=c.mem.r32(c.arg(3)),size=requested;auto bytes=requested?std::vector<uint8_t>(requested):std::vector<uint8_t>{};HRESULT hr=resource_of(static_cast<IUnknown*>(p.host))->GetPrivateData(key,requested?bytes.data():nullptr,&size);if(c.arg(2)&&size)c.mem.write_bytes(c.arg(2),bytes.data(),std::min<DWORD>(size,requested));c.mem.w32(c.arg(3),size);return uint32_t(hr);});
        method(iface,6,2,[resource_of](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;GUID key=d3d9_read_struct<GUID>(c,c.arg(1));return uint32_t(resource_of(static_cast<IUnknown*>(p.host))->FreePrivateData(key));});
        method(iface,7,2,[resource_of](Proxy& p,Cpu& c){return resource_of(static_cast<IUnknown*>(p.host))->SetPriority(c.arg(1));});
        method(iface,8,1,[resource_of](Proxy& p,Cpu&){return resource_of(static_cast<IUnknown*>(p.host))->GetPriority();});
        method(iface,9,1,[resource_of](Proxy& p,Cpu&){resource_of(static_cast<IUnknown*>(p.host))->PreLoad();return 0u;});
        method(iface,10,1,[resource_of](Proxy& p,Cpu&){return uint32_t(resource_of(static_cast<IUnknown*>(p.host))->GetType());});
    };
    register_resource("IDirect3DSurface9",[](IUnknown* object){return static_cast<IDirect3DResource9*>(static_cast<IDirect3DSurface9*>(object));});
    register_resource("IDirect3DTexture9",[](IUnknown* object){return static_cast<IDirect3DResource9*>(static_cast<IDirect3DTexture9*>(object));});
    method("IDirect3DSurface9",11,3,[&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1)||!c.arg(2))return E_POINTER;GUID iid=d3d9_read_struct<GUID>(c,c.arg(1));void* raw=nullptr;HRESULT hr=static_cast<IDirect3DSurface9*>(p.host)->GetContainer(iid,&raw);IUnknown* x=static_cast<IUnknown*>(raw);if(SUCCEEDED(hr)&&IsEqualGUID(iid,D3D9_IID_TEXTURE))c.mem.w32(c.arg(2),d3d9_wrap(s,c,"IDirect3DTexture9",x));else {if(x)x->Release();c.mem.w32(c.arg(2),0);if(SUCCEEDED(hr))hr=E_NOINTERFACE;}return uint32_t(hr);});
    method("IDirect3DSurface9",12,2,[](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;D3DSURFACE_DESC x{};HRESULT hr=static_cast<IDirect3DSurface9*>(p.host)->GetDesc(&x);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(1),&x,sizeof(x));return uint32_t(hr);});
    method("IDirect3DSurface9",13,4,[&h](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;RECT rect{};RECT* region=nullptr;if(c.arg(2)){rect=d3d9_read_struct<RECT>(c,c.arg(2));region=&rect;}D3DLOCKED_RECT x{};HRESULT hr=static_cast<IDirect3DSurface9*>(p.host)->LockRect(&x,region,c.arg(3));if(SUCCEEDED(hr)){D3DSURFACE_DESC desc{};static_cast<IDirect3DSurface9*>(p.host)->GetDesc(&desc);uint32_t rows=region?uint32_t(std::max<LONG>(0,rect.bottom-rect.top)):desc.Height;uint32_t pitch=uint32_t(std::abs(x.Pitch));uint64_t bytes=uint64_t(pitch)*rows;if(!x.pBits||!bytes||bytes>D3D9_GUEST_SIZE){static_cast<IDirect3DSurface9*>(p.host)->UnlockRect();return D3DERR_INVALIDCALL;}uint32_t size=uint32_t(bytes);uint32_t at=d3d9_guest_buffer(h,c,size);c.mem.write_bytes(at,x.pBits,size);h.d3d9_locks[p.guest]={x.pBits,size,at};c.mem.w32(c.arg(1),uint32_t(x.Pitch));c.mem.w32(c.arg(1)+4,at);}return uint32_t(hr);});
    method("IDirect3DSurface9",14,1,[&h](Proxy& p,Cpu& c)->uint32_t {auto it=h.d3d9_locks.find(p.guest);if(it!=h.d3d9_locks.end()&&std::get<0>(it->second)&&std::get<1>(it->second)){auto bytes=c.mem.read_bytes(std::get<2>(it->second),std::get<1>(it->second));memcpy(std::get<0>(it->second),bytes.data(),bytes.size());}HRESULT hr=static_cast<IDirect3DSurface9*>(p.host)->UnlockRect();h.d3d9_locks.erase(p.guest);return uint32_t(hr);});
    method("IDirect3DSurface9",15,2,[&h](Proxy& p,Cpu& c)->uint32_t {d3d9_gap(h,p,c,"GetDC unsupported");return D3D9_E_NOTIMPL;});
    method("IDirect3DSurface9",16,2,[&h](Proxy& p,Cpu& c)->uint32_t {d3d9_gap(h,p,c,"ReleaseDC unsupported");return D3D9_E_NOTIMPL;});
    method("IDirect3DTexture9",11,2,[](Proxy& p,Cpu& c){return static_cast<IDirect3DTexture9*>(p.host)->SetLOD(c.arg(1));});
    method("IDirect3DTexture9",12,1,[](Proxy& p,Cpu&){return static_cast<IDirect3DTexture9*>(p.host)->GetLOD();});
    method("IDirect3DTexture9",13,1,[](Proxy& p,Cpu&){return static_cast<IDirect3DTexture9*>(p.host)->GetLevelCount();});
    method("IDirect3DTexture9",14,2,[](Proxy& p,Cpu& c)->uint32_t {return uint32_t(static_cast<IDirect3DTexture9*>(p.host)->SetAutoGenFilterType(D3DTEXTUREFILTERTYPE(c.arg(1))));});
    method("IDirect3DTexture9",15,1,[](Proxy& p,Cpu&){return uint32_t(static_cast<IDirect3DTexture9*>(p.host)->GetAutoGenFilterType());});
    method("IDirect3DTexture9",16,1,[](Proxy& p,Cpu&){static_cast<IDirect3DTexture9*>(p.host)->GenerateMipSubLevels();return 0u;});
    method("IDirect3DTexture9",17,3,[](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;D3DSURFACE_DESC x{};HRESULT hr=static_cast<IDirect3DTexture9*>(p.host)->GetLevelDesc(c.arg(1),&x);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(2),&x,sizeof(x));return uint32_t(hr);});
    method("IDirect3DTexture9",18,3,[&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;IDirect3DSurface9* x=nullptr;HRESULT hr=static_cast<IDirect3DTexture9*>(p.host)->GetSurfaceLevel(c.arg(1),&x);if(SUCCEEDED(hr))c.mem.w32(c.arg(2),d3d9_wrap(s,c,"IDirect3DSurface9",x));else c.mem.w32(c.arg(2),0);return uint32_t(hr);});
    method("IDirect3DTexture9",19,5,[&h](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;RECT rect{};RECT* region=nullptr;if(c.arg(3)){rect=d3d9_read_struct<RECT>(c,c.arg(3));region=&rect;}D3DLOCKED_RECT x{};auto* texture=static_cast<IDirect3DTexture9*>(p.host);HRESULT hr=texture->LockRect(c.arg(1),&x,region,c.arg(4));if(SUCCEEDED(hr)){D3DSURFACE_DESC desc{};if(FAILED(texture->GetLevelDesc(c.arg(1),&desc))){texture->UnlockRect(c.arg(1));return D3DERR_INVALIDCALL;}uint32_t rows=region?uint32_t(std::max<LONG>(0,rect.bottom-rect.top)):desc.Height;uint32_t pitch=uint32_t(std::abs(x.Pitch));uint64_t bytes=uint64_t(pitch)*rows;if(!x.pBits||!bytes||bytes>D3D9_GUEST_SIZE){texture->UnlockRect(c.arg(1));return D3DERR_INVALIDCALL;}uint32_t size=uint32_t(bytes),at=d3d9_guest_buffer(h,c,size);c.mem.write_bytes(at,x.pBits,size);h.d3d9_locks[p.guest]={x.pBits,size,at};c.mem.w32(c.arg(2),uint32_t(x.Pitch));c.mem.w32(c.arg(2)+4,at);}return uint32_t(hr);});
    method("IDirect3DTexture9",20,2,[&h](Proxy& p,Cpu& c)->uint32_t {auto it=h.d3d9_locks.find(p.guest);if(it!=h.d3d9_locks.end()&&std::get<0>(it->second)&&std::get<1>(it->second)){auto b=c.mem.read_bytes(std::get<2>(it->second),std::get<1>(it->second));memcpy(std::get<0>(it->second),b.data(),b.size());}HRESULT hr=static_cast<IDirect3DTexture9*>(p.host)->UnlockRect(c.arg(1));h.d3d9_locks.erase(p.guest);return uint32_t(hr);});
    method("IDirect3DTexture9",21,2,[](Proxy& p,Cpu& c)->uint32_t {RECT x{},*r=nullptr;if(c.arg(1)){x=d3d9_read_struct<RECT>(c,c.arg(1));r=&x;}return uint32_t(static_cast<IDirect3DTexture9*>(p.host)->AddDirtyRect(r));});
    method("IDirect3DIndexBuffer9",11,5,[&h](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(3))return E_POINTER;auto* buffer=static_cast<IDirect3DIndexBuffer9*>(p.host);D3DINDEXBUFFER_DESC desc{};if(FAILED(buffer->GetDesc(&desc))||c.arg(1)>desc.Size||(c.arg(2)&&c.arg(2)>desc.Size-c.arg(1)))return D3DERR_INVALIDCALL;void* x=nullptr;HRESULT hr=buffer->Lock(c.arg(1),c.arg(2),&x,c.arg(4));if(SUCCEEDED(hr)){uint32_t size=c.arg(2)?c.arg(2):desc.Size-c.arg(1);if(!x||!size){buffer->Unlock();return D3DERR_INVALIDCALL;}uint32_t at=d3d9_guest_buffer(h,c,size);c.mem.write_bytes(at,x,size);h.d3d9_locks[p.guest]={x,size,at};c.mem.w32(c.arg(3),at);}return uint32_t(hr);});
    method("IDirect3DIndexBuffer9",12,1,[&h](Proxy& p,Cpu& c)->uint32_t {auto it=h.d3d9_locks.find(p.guest);if(it!=h.d3d9_locks.end()&&std::get<0>(it->second)&&std::get<1>(it->second)){auto b=c.mem.read_bytes(std::get<2>(it->second),std::get<1>(it->second));memcpy(std::get<0>(it->second),b.data(),b.size());}HRESULT hr=static_cast<IDirect3DIndexBuffer9*>(p.host)->Unlock();h.d3d9_locks.erase(p.guest);return uint32_t(hr);});
    method("IDirect3DIndexBuffer9",13,2,[](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;D3DINDEXBUFFER_DESC x{};HRESULT hr=static_cast<IDirect3DIndexBuffer9*>(p.host)->GetDesc(&x);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(1),&x,sizeof(x));return uint32_t(hr);});
    method("IDirect3DSwapChain9",3,6,[&h](Proxy& p,Cpu& c)->uint32_t {if(c.arg(4)){d3d9_gap(h,p,c,"Present dirty-region");return D3D9_E_NOTIMPL;}RECT a{},b{};RECT* ap=nullptr;RECT* bp=nullptr;if(c.arg(1)){a=d3d9_read_struct<RECT>(c,c.arg(1));ap=&a;}if(c.arg(2)){b=d3d9_read_struct<RECT>(c,c.arg(2));bp=&b;}HWND window=reinterpret_cast<HWND>(h.d3d9_window?h.d3d9_window(c.arg(3)):(c.arg(3)?0:0));return uint32_t(static_cast<IDirect3DSwapChain9*>(p.host)->Present(ap,bp,window,nullptr,c.arg(5)));});
    method("IDirect3DSwapChain9",5,4,[&s](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(3))return E_POINTER;IDirect3DSurface9* x=nullptr;HRESULT hr=static_cast<IDirect3DSwapChain9*>(p.host)->GetBackBuffer(c.arg(1),D3DBACKBUFFER_TYPE(c.arg(2)),&x);if(SUCCEEDED(hr))c.mem.w32(c.arg(3),d3d9_wrap(s,c,"IDirect3DSurface9",x));else c.mem.w32(c.arg(3),0);return uint32_t(hr);});
    method("IDirect3DSwapChain9",9,2,[](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;D3DPRESENT_PARAMETERS x{};HRESULT hr=static_cast<IDirect3DSwapChain9*>(p.host)->GetPresentParameters(&x);if(SUCCEEDED(hr))d3d9_write_present_parameters(c,c.arg(1),x);return uint32_t(hr);});
    method("IDirect3DStateBlock9",4,1,[](Proxy& p,Cpu&){return uint32_t(static_cast<IDirect3DStateBlock9*>(p.host)->Capture());});
    method("IDirect3DStateBlock9",5,1,[](Proxy& p,Cpu&){return uint32_t(static_cast<IDirect3DStateBlock9*>(p.host)->Apply());});
    method("IDirect3DVertexBuffer9",11,5,[&h](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(3))return E_POINTER;auto* buffer=static_cast<IDirect3DVertexBuffer9*>(p.host);D3DVERTEXBUFFER_DESC desc{};if(FAILED(buffer->GetDesc(&desc))||c.arg(1)>desc.Size||(c.arg(2)&&c.arg(2)>desc.Size-c.arg(1)))return D3DERR_INVALIDCALL;void* x=nullptr;HRESULT hr=buffer->Lock(c.arg(1),c.arg(2),&x,c.arg(4));if(SUCCEEDED(hr)){uint32_t size=c.arg(2)?c.arg(2):desc.Size-c.arg(1);if(!x||!size){buffer->Unlock();return D3DERR_INVALIDCALL;}uint32_t at=d3d9_guest_buffer(h,c,size);c.mem.write_bytes(at,x,size);h.d3d9_locks[p.guest]={x,size,at};c.mem.w32(c.arg(3),at);}return uint32_t(hr);});
    method("IDirect3DVertexBuffer9",12,1,[&h](Proxy& p,Cpu& c)->uint32_t {auto it=h.d3d9_locks.find(p.guest);if(it!=h.d3d9_locks.end()&&std::get<0>(it->second)&&std::get<1>(it->second)){auto bytes=c.mem.read_bytes(std::get<2>(it->second),std::get<1>(it->second));memcpy(std::get<0>(it->second),bytes.data(),bytes.size());}HRESULT hr=static_cast<IDirect3DVertexBuffer9*>(p.host)->Unlock();h.d3d9_locks.erase(p.guest);return uint32_t(hr);});
    static constexpr unsigned d3d9_arities[]={3,1,1,2,1,4,3,5,3,6,7,7,6,5,4,2,7};
    static constexpr unsigned d3d9ex_arities[]={3,1,1,2,1,4,3,5,3,6,7,7,6,5,4,2,7,3,5,4,8,3};
    static constexpr unsigned device_arities[]={3,1,1,1,1,1,2,2,3,2,4,4,2,3,3,1,2,5,5,3,2,4,3,9,10,8,7,7,9,9,5,3,3,3,6,4,7,3,3,2,2,1,1,7,3,3,3,2,2,2,2,3,3,3,3,3,3,3,3,3,1,2,2,2,3,3,4,4,4,4,2,3,3,2,2,2,2,2,1,2,1,4,7,5,9,7,3,2,2,2,2,3,2,2,4,4,4,4,4,4,5,5,3,3,2,2,3,2,2,4,4,4,4,4,4,4,4,2,3};
    static constexpr unsigned surface_arities[]={3,1,1,2,5,4,2,2,1,1,1,3,2,4,1,2,2};
    static constexpr unsigned buffer_arities[]={3,1,1,2,5,4,2,2,1,1,1,5,1,2};
    static constexpr unsigned shader_arities[]={3,1,1,2,3};
    static constexpr unsigned texture_arities[]={3,1,1,2,5,4,2,2,1,1,1,2,1,1,2,1,1,3,3,5,2,2};
    static constexpr unsigned swapchain_arities[]={3,1,1,6,2,4,2,2,2,2};
    static constexpr unsigned stateblock_arities[]={3,1,1,2,1,1,1};
    static constexpr unsigned declaration_arities[]={3,1,1,2,3};
    d3d9_register_gaps(s,h,"IDirect3D9",d3d9_arities);
    d3d9_register_gaps(s,h,"IDirect3D9Ex",d3d9ex_arities);
    d3d9_register_gaps(s,h,"IDirect3DDevice9",device_arities);
    d3d9_register_gaps(s,h,"IDirect3DSurface9",surface_arities);
    d3d9_register_gaps(s,h,"IDirect3DVertexBuffer9",buffer_arities);
    d3d9_register_gaps(s,h,"IDirect3DIndexBuffer9",buffer_arities);
    d3d9_register_gaps(s,h,"IDirect3DTexture9",texture_arities);
    d3d9_register_gaps(s,h,"IDirect3DSwapChain9",swapchain_arities);
    d3d9_register_gaps(s,h,"IDirect3DStateBlock9",stateblock_arities);
    d3d9_register_gaps(s,h,"IDirect3DVertexDeclaration9",declaration_arities);
    d3d9_register_gaps(s,h,"IDirect3DVertexShader9",shader_arities);
    d3d9_register_gaps(s,h,"IDirect3DPixelShader9",shader_arities);
}
inline void register_d3d9(Registry& r, Host& h) {
    register_d3d9_com(h.com,h);
    r.add("d3d9.dll","Direct3DCreate9",Conv::Stdcall,1,[&h](Cpu& c)->uint32_t {if(!h.d3d9_factory)return 0;uintptr_t result=0;h.d3d9_factory(false,c.arg(0)==0x80000020u?0x20u:c.arg(0),&result);return result?d3d9_wrap(h.com,c,"IDirect3D9",reinterpret_cast<IUnknown*>(result)):0;});
    r.add("d3d9.dll","Direct3DCreate9Ex",Conv::Stdcall,2,[&h](Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;if(!h.d3d9_factory){c.mem.w32(c.arg(1),0);return E_NOTIMPL;}uintptr_t result=0;HRESULT hr=HRESULT(h.d3d9_factory(true,c.arg(0)==0x80000020u?0x20u:c.arg(0),&result));c.mem.w32(c.arg(1),result?d3d9_wrap(h.com,c,"IDirect3D9Ex",reinterpret_cast<IUnknown*>(result)):0);return uint32_t(hr);});
}
} // namespace x86
#else
namespace x86 { inline void register_d3d9(Registry&, Host&) {} }
#endif
