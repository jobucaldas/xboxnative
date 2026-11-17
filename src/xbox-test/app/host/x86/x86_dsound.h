// SPDX-License-Identifier: GPL-3.0-or-later
// PE32 DirectSound exports and COM marshalling onto the in-process XAudio2 shim.
#pragma once
#include <cstring>
#include <map>
#include <vector>
#include "x86_d3d9.h"
#if defined(_WIN32)
#include <windows.h>
#pragma push_macro("WINAPI_FAMILY")
#undef WINAPI_FAMILY
#define WINAPI_FAMILY WINAPI_FAMILY_DESKTOP_APP
#include <mmsystem.h>
#include <dsound.h>
#pragma pop_macro("WINAPI_FAMILY")
namespace x86 {
// Interface IDs defined here rather than taken from dxguid/uuid: the launcher's
// UWP link does not pull those import libraries in, so referencing IID_* symbols
// left unresolved externals in every binary that includes this header.
namespace dsound_iid {
inline constexpr GUID unknown{0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
inline constexpr GUID sound{0x279AFA83,0x4981,0x11CE,{0xA5,0x21,0x00,0x20,0xAF,0x0B,0xE5,0x60}};
inline constexpr GUID sound8{0xC50A7E93,0xF395,0x4834,{0x9E,0xF6,0x7F,0xA9,0x9D,0xE5,0x09,0x66}};
inline constexpr GUID buffer{0x279AFA85,0x4981,0x11CE,{0xA5,0x21,0x00,0x20,0xAF,0x0B,0xE5,0x60}};
inline constexpr GUID buffer8{0x6825A449,0x7524,0x4D82,{0x92,0x0F,0x50,0xE3,0x6A,0xB3,0xAB,0x1E}};
inline constexpr GUID notify{0xB0210783,0x89CD,0x11D0,{0xAF,0x08,0x00,0xA0,0xC9,0x25,0xCD,0x16}};
}
inline constexpr uint32_t DS_E_POINTER=0x80004003u;
inline constexpr uint32_t DS_E_NOINTERFACE=0x80004002u;
inline constexpr uint32_t DSERR_NODRIVER_X86=0x88780078u;

inline uint32_t dsound_wrap(ComSpace& space,Cpu& cpu,const char* iface,IUnknown* object) {
    if(!object)return 0;
    uint32_t guest=space.create(cpu,iface,object,[object](){object->AddRef();},[object](){object->Release();});
    object->Release();
    return guest;
}
inline uint32_t dsound_guid(Cpu& cpu,uint32_t address,GUID& value) {
    if(!address)return 0;
    auto bytes=cpu.mem.read_bytes(address,sizeof(value));memcpy(&value,bytes.data(),sizeof(value));return 1;
}
inline HRESULT dsound_qi(Proxy& proxy,Cpu& cpu) {
    const uint32_t iid_address=cpu.arg(1),out=cpu.arg(2);if(!out)return E_POINTER;cpu.mem.w32(out,0);
    GUID iid{};if(!dsound_guid(cpu,iid_address,iid))return E_POINTER;
    void* result=nullptr;HRESULT hr=static_cast<IUnknown*>(proxy.host)->QueryInterface(iid,&result);if(FAILED(hr))return hr;
    const char* iface=nullptr;
    if(IsEqualIID(iid,dsound_iid::unknown))iface=strcmp(proxy.iface,"IDirectSoundNotify")==0?"IDirectSoundBuffer8":proxy.iface;
    else if(IsEqualIID(iid,dsound_iid::sound8)||IsEqualIID(iid,dsound_iid::sound))iface="IDirectSound8";
    else if(IsEqualIID(iid,dsound_iid::buffer8)||IsEqualIID(iid,dsound_iid::buffer))iface="IDirectSoundBuffer8";
    else if(IsEqualIID(iid,dsound_iid::notify))iface="IDirectSoundNotify";
    if(!iface){static_cast<IUnknown*>(result)->Release();return E_NOINTERFACE;}
    cpu.mem.w32(out,dsound_wrap(*proxy.space,cpu,iface,static_cast<IUnknown*>(result)));return hr;
}
struct DSoundLock {void* first=nullptr;void* second=nullptr;DWORD first_bytes=0,second_bytes=0;uint32_t guest=0;};
inline std::map<uint32_t,DSoundLock>& dsound_locks(){static std::map<uint32_t,DSoundLock> locks;return locks;}
inline void register_dsound_com(ComSpace& s,Host& h) {
    s.interface_("IDirectSound8",12);s.interface_("IDirectSoundBuffer8",24);s.interface_("IDirectSoundNotify",4);
    auto method=[&](const char* iface,unsigned slot,unsigned argc,Marshaller fn){s.method(iface,slot,argc,std::move(fn));};
    for(const char* iface:{"IDirectSound8","IDirectSoundBuffer8","IDirectSoundNotify"})method(iface,0,3,[](Proxy& p,Cpu& c){return uint32_t(dsound_qi(p,c));});
    auto device=[](Proxy& p){return static_cast<IDirectSound8*>(p.host);};
    auto buffer=[](Proxy& p){return static_cast<IDirectSoundBuffer8*>(p.host);};
    method("IDirectSound8",3,4,[&s,device](Proxy& p,Cpu& c)->uint32_t {
        if(!c.arg(2))return E_POINTER;uint32_t out=c.arg(2);c.mem.w32(out,0);if(c.arg(3))return DSERR_NOAGGREGATION;DSBUFFERDESC desc{};desc.dwSize=sizeof(desc);
        uint32_t addr=c.arg(1);if(!addr)return E_POINTER;if(c.mem.r32(addr)<36u)return DSERR_INVALIDPARAM;
        desc.dwFlags=c.mem.r32(addr+4);desc.dwBufferBytes=c.mem.r32(addr+8);desc.dwReserved=c.mem.r32(addr+12);
        uint32_t fmt_addr=c.mem.r32(addr+16);WAVEFORMATEX fmt{};if(fmt_addr){auto bytes=c.mem.read_bytes(fmt_addr,sizeof(fmt));memcpy(&fmt,bytes.data(),sizeof(fmt));desc.lpwfxFormat=&fmt;}
        IDirectSoundBuffer* object=nullptr;HRESULT hr=device(p)->CreateSoundBuffer(&desc,&object,reinterpret_cast<IUnknown*>(uintptr_t(c.arg(3))));
        if(SUCCEEDED(hr)&&object)c.mem.w32(out,dsound_wrap(s,c,"IDirectSoundBuffer8",reinterpret_cast<IUnknown*>(object)));return uint32_t(hr);
    });
    method("IDirectSound8",4,2,[device](Proxy& p,Cpu& c)->uint32_t {uint32_t out=c.arg(1);if(!out)return E_POINTER;DSCAPS caps{};caps.dwSize=sizeof(caps);HRESULT hr=device(p)->GetCaps(&caps);if(SUCCEEDED(hr))c.mem.write_bytes(out,&caps,sizeof(caps));return uint32_t(hr);});
    method("IDirectSound8",5,3,[&s,device](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(2))return E_POINTER;c.mem.w32(c.arg(2),0);uint32_t guest=c.arg(1);auto& original=p.space->by_guest(guest);IDirectSoundBuffer* duplicate=nullptr;HRESULT hr=device(p)->DuplicateSoundBuffer(static_cast<IDirectSoundBuffer*>(original.host),&duplicate);if(SUCCEEDED(hr)&&duplicate)c.mem.w32(c.arg(2),dsound_wrap(s,c,"IDirectSoundBuffer8",reinterpret_cast<IUnknown*>(duplicate)));return uint32_t(hr);});
    method("IDirectSound8",6,3,[device](Proxy& p,Cpu& c){return uint32_t(device(p)->SetCooperativeLevel(reinterpret_cast<HWND>(uintptr_t(c.arg(1))),c.arg(2)));});
    method("IDirectSound8",7,1,[device](Proxy& p,Cpu&){return uint32_t(device(p)->Compact());});
    method("IDirectSound8",8,2,[device](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;DWORD config=0;HRESULT hr=device(p)->GetSpeakerConfig(&config);if(SUCCEEDED(hr))c.mem.w32(c.arg(1),config);return uint32_t(hr);});
    method("IDirectSound8",9,2,[device](Proxy& p,Cpu& c){return uint32_t(device(p)->SetSpeakerConfig(c.arg(1)));});
    method("IDirectSound8",10,2,[device](Proxy& p,Cpu& c)->uint32_t {GUID id{};GUID* idp=nullptr;if(c.arg(1)){auto bytes=c.mem.read_bytes(c.arg(1),sizeof(id));memcpy(&id,bytes.data(),sizeof(id));idp=&id;}return uint32_t(device(p)->Initialize(idp));});
    method("IDirectSound8",11,2,[device](Proxy& p,Cpu& c)->uint32_t {DWORD certified=0;HRESULT hr=device(p)->VerifyCertification(&certified);if(SUCCEEDED(hr)&&c.arg(1))c.mem.w32(c.arg(1),certified);return uint32_t(hr);});
    method("IDirectSoundBuffer8",3,2,[buffer](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;DSBCAPS caps{};caps.dwSize=sizeof(caps);HRESULT hr=buffer(p)->GetCaps(&caps);if(SUCCEEDED(hr))c.mem.write_bytes(c.arg(1),&caps,sizeof(caps));return uint32_t(hr);});
    method("IDirectSoundBuffer8",4,3,[buffer](Proxy& p,Cpu& c)->uint32_t {DWORD play=0,write=0;HRESULT hr=buffer(p)->GetCurrentPosition(c.arg(1)?&play:nullptr,c.arg(2)?&write:nullptr);if(SUCCEEDED(hr)){if(c.arg(1))c.mem.w32(c.arg(1),play);if(c.arg(2))c.mem.w32(c.arg(2),write);}return uint32_t(hr);});
    method("IDirectSoundBuffer8",5,4,[buffer](Proxy& p,Cpu& c)->uint32_t {DWORD written=0;DWORD cap=c.arg(2);if(cap&&!c.arg(1))return E_POINTER;WAVEFORMATEX fmt{};HRESULT hr=buffer(p)->GetFormat(cap?&fmt:nullptr,sizeof(fmt),c.arg(3)?&written:nullptr);if(SUCCEEDED(hr)){if(cap)c.mem.write_bytes(c.arg(1),&fmt,std::min<DWORD>(cap,sizeof(fmt)));if(c.arg(3))c.mem.w32(c.arg(3),written);}return uint32_t(hr);});
    for(unsigned slot=6;slot<=9;++slot)method("IDirectSoundBuffer8",slot,2,[buffer,slot](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;DWORD value=0;LONG long_value=0;HRESULT hr=E_NOTIMPL;if(slot==6){hr=buffer(p)->GetVolume(&long_value);value=DWORD(long_value);}else if(slot==7){hr=buffer(p)->GetPan(&long_value);value=DWORD(long_value);}else if(slot==8)hr=buffer(p)->GetFrequency(&value);else hr=buffer(p)->GetStatus(&value);if(SUCCEEDED(hr))c.mem.w32(c.arg(1),value);return uint32_t(hr);});
    method("IDirectSoundBuffer8",10,3,[buffer](Proxy& p,Cpu&)->uint32_t {return uint32_t(buffer(p)->Initialize(nullptr,nullptr));});
    method("IDirectSoundBuffer8",11,8,[&h,buffer](Proxy& p,Cpu& c)->uint32_t {
        uint32_t out1=c.arg(3),outn1=c.arg(4),out2=c.arg(5),outn2=c.arg(6);if(!out1||!outn1||!out2||!outn2)return E_POINTER;
        void *first=nullptr,*second=nullptr;DWORD n1=0,n2=0;HRESULT hr=buffer(p)->Lock(c.arg(1),c.arg(2),&first,&n1,&second,&n2,c.arg(7));if(FAILED(hr))return uint32_t(hr);
        uint32_t guest=d3d9_guest_buffer(h,c,n1+n2);if(n1)c.mem.write_bytes(guest,first,n1);if(n2)c.mem.write_bytes(guest+n1,second,n2);
        dsound_locks()[p.guest]={first,second,n1,n2,guest};c.mem.w32(out1,guest);c.mem.w32(outn1,n1);c.mem.w32(out2,n2?guest+n1:0);c.mem.w32(outn2,n2);return uint32_t(hr);
    });
    method("IDirectSoundBuffer8",12,4,[buffer](Proxy& p,Cpu& c){return uint32_t(buffer(p)->Play(c.arg(1),c.arg(2),c.arg(3)));});
    method("IDirectSoundBuffer8",13,2,[buffer](Proxy& p,Cpu& c){return uint32_t(buffer(p)->SetCurrentPosition(c.arg(1)));});
    method("IDirectSoundBuffer8",14,2,[buffer](Proxy& p,Cpu& c)->uint32_t {if(!c.arg(1))return E_POINTER;WAVEFORMATEX fmt{};auto bytes=c.mem.read_bytes(c.arg(1),sizeof(fmt));memcpy(&fmt,bytes.data(),sizeof(fmt));return uint32_t(buffer(p)->SetFormat(&fmt));});
    method("IDirectSoundBuffer8",15,2,[buffer](Proxy& p,Cpu& c){return uint32_t(buffer(p)->SetVolume(LONG(c.arg(1))));});
    method("IDirectSoundBuffer8",16,2,[buffer](Proxy& p,Cpu& c){return uint32_t(buffer(p)->SetPan(LONG(c.arg(1))));});
    method("IDirectSoundBuffer8",17,2,[buffer](Proxy& p,Cpu& c){return uint32_t(buffer(p)->SetFrequency(c.arg(1)));});
    method("IDirectSoundBuffer8",18,1,[buffer](Proxy& p,Cpu& c){return uint32_t(buffer(p)->Stop());});
    method("IDirectSoundBuffer8",19,5,[](Proxy& p,Cpu& c)->uint32_t {auto it=dsound_locks().find(p.guest);if(it==dsound_locks().end())return DSERR_INVALIDCALL;DSoundLock lock=it->second;uint32_t a=c.arg(1),n1=c.arg(2),b=c.arg(3),n2=c.arg(4);if(a!=lock.guest||n1!=lock.first_bytes||b!=(lock.second_bytes?lock.guest+lock.first_bytes:0)||n2!=lock.second_bytes)return DSERR_INVALIDPARAM;if(n1){auto data=c.mem.read_bytes(a,n1);memcpy(lock.first,data.data(),n1);}if(n2){auto data=c.mem.read_bytes(b,n2);memcpy(lock.second,data.data(),n2);}HRESULT hr=static_cast<IDirectSoundBuffer8*>(p.host)->Unlock(lock.first,n1,lock.second,n2);dsound_locks().erase(it);return uint32_t(hr);});
    method("IDirectSoundBuffer8",20,1,[buffer](Proxy& p,Cpu&){return uint32_t(buffer(p)->Restore());});
    method("IDirectSoundBuffer8",21,4,[](Proxy&,Cpu&){return uint32_t(DSERR_FXUNAVAILABLE);});
    method("IDirectSoundBuffer8",22,4,[](Proxy&,Cpu&){return uint32_t(DSERR_UNSUPPORTED);});
    method("IDirectSoundBuffer8",23,5,[](Proxy&,Cpu& c)->uint32_t {if(c.arg(4))c.mem.w32(c.arg(4),0);return uint32_t(DSERR_OBJECTNOTFOUND);});
    method("IDirectSoundNotify",3,3,[&h](Proxy& p,Cpu& c)->uint32_t {DWORD count=c.arg(1);if(count>64||(!c.arg(2)&&count))return E_INVALIDARG;DSBPOSITIONNOTIFY list[64]{};for(DWORD i=0;i<count;++i){uint32_t address=c.arg(2)+i*8;uint32_t token=c.mem.r32(address+4);uintptr_t event=h.dsound_event?h.dsound_event(token):0;if(!event)return E_INVALIDARG;list[i].dwOffset=c.mem.r32(address);list[i].hEventNotify=reinterpret_cast<HANDLE>(event);}return uint32_t(static_cast<IDirectSoundNotify*>(p.host)->SetNotificationPositions(count,list));});
}
inline void register_dsound(Registry& r,Host& h) {
    register_dsound_com(h.com,h);
    auto create=[&h](Cpu& c,bool)->uint32_t {if(!c.arg(1))return E_POINTER;c.mem.w32(c.arg(1),0);if(c.arg(2))return DSERR_NOAGGREGATION;GUID id{},*idp=nullptr;if(c.arg(0)){auto bytes=c.mem.read_bytes(c.arg(0),sizeof(id));memcpy(&id,bytes.data(),sizeof(id));idp=&id;}if(!h.dsound_factory)return DSERR_NODRIVER_X86;uintptr_t result=0;uint32_t hr=uint32_t(h.dsound_factory(idp,&result));if(FAILED(HRESULT(hr))||!result)return FAILED(HRESULT(hr))?hr:DSERR_NODRIVER_X86;c.mem.w32(c.arg(1),dsound_wrap(h.com,c,"IDirectSound8",reinterpret_cast<IUnknown*>(result)));return hr;};
    r.add("dsound.dll","DirectSoundCreate",Conv::Stdcall,3,[create](Cpu& c){return create(c,false);});
    r.add("dsound.dll","#1",Conv::Stdcall,3,[create](Cpu& c){return create(c,false);});
    r.add("dsound.dll","DirectSoundCreate8",Conv::Stdcall,3,[create](Cpu& c){return create(c,true);});
    r.add("dsound.dll","#11",Conv::Stdcall,3,[create](Cpu& c){return create(c,true);});
    auto get_device_id=[](Cpu& c)->uint32_t {if(!c.arg(0)||!c.arg(1))return E_POINTER;GUID source{},destination={0xDEF00000,0x9C6D,0x47ED,{0xAA,0xF1,0x4D,0xDA,0x8F,0x2B,0x5C,0x03}};auto bytes=c.mem.read_bytes(c.arg(0),sizeof(source));memcpy(&source,bytes.data(),sizeof(source));if(!IsEqualGUID(source,destination))return DSERR_NODRIVER_X86;c.mem.write_bytes(c.arg(1),&destination,sizeof(destination));return S_OK;};
    r.add("dsound.dll","#7",Conv::Stdcall,2,get_device_id);
    r.add("dsound.dll","GetDeviceID",Conv::Stdcall,2,get_device_id);
    auto enumerate=[&h](Cpu& c,bool wide)->uint32_t {
        const uint32_t callback=c.arg(0),context=c.arg(1);if(!callback)return E_INVALIDARG;
        if(!h.dsound_factory)return DSERR_NODRIVER_X86;uintptr_t device=0;HRESULT available=HRESULT(h.dsound_factory(nullptr,&device));if(FAILED(available)||!device)return FAILED(available)?uint32_t(available):DSERR_NODRIVER_X86;reinterpret_cast<IUnknown*>(device)->Release();
        static const char description_a[]="Default DirectSound Device",module_a[]="dsound.dll";
        static const char16_t description_w[]=u"Default DirectSound Device",module_w[]=u"dsound.dll";
        const uint32_t desc_size=wide?uint32_t(sizeof(description_w)):uint32_t(sizeof(description_a));
        const uint32_t mod_size=wide?uint32_t(sizeof(module_w)):uint32_t(sizeof(module_a));
        uint32_t desc=d3d9_guest_buffer(h,c,desc_size),module=d3d9_guest_buffer(h,c,mod_size);
        if(wide){c.mem.write_bytes(desc,description_w,desc_size);c.mem.write_bytes(module,module_w,mod_size);}
        else {c.mem.write_bytes(desc,description_a,desc_size);c.mem.write_bytes(module,module_a,mod_size);}
        const GUID id={0xDEF00000,0x9C6D,0x47ED,{0xAA,0xF1,0x4D,0xDA,0x8F,0x2B,0x5C,0x03}};
        uint32_t guid=d3d9_guest_buffer(h,c,sizeof(id));c.mem.write_bytes(guid,&id,sizeof(id));
        c.call(callback,{guid,desc,module,context});return S_OK;
    };
    r.add("dsound.dll","DirectSoundEnumerateA",Conv::Stdcall,2,[enumerate](Cpu& c){return enumerate(c,false);});
    r.add("dsound.dll","#2",Conv::Stdcall,2,[enumerate](Cpu& c){return enumerate(c,false);});
    r.add("dsound.dll","DirectSoundEnumerateW",Conv::Stdcall,2,[enumerate](Cpu& c){return enumerate(c,true);});
    r.add("dsound.dll","#3",Conv::Stdcall,2,[enumerate](Cpu& c){return enumerate(c,true);});
    r.add("dsound.dll","DirectSoundCaptureEnumerateW",Conv::Stdcall,2,[](Cpu&){return DSERR_NODRIVER_X86;});
}
}
#else
namespace x86 { inline void register_dsound(Registry&,Host&) {} }
#endif
