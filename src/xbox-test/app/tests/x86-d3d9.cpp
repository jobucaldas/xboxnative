// SPDX-License-Identifier: GPL-3.0-or-later
// Runs a real i686 guest against the x64 D3D9 shim in the same Wine process.
#include <cstdio>
#include <string>
#include <vector>
#include "../host/x86/x86_play.h"

static std::vector<uint8_t> read_file(const char* path) {
    HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return {};
    LARGE_INTEGER length{};if(!GetFileSizeEx(file,&length)||length.QuadPart<=0||length.QuadPart>256ll*1024*1024){CloseHandle(file);return {};}
    std::vector<uint8_t> bytes(size_t(length.QuadPart));DWORD read=0;
    bool ok=ReadFile(file,bytes.data(),DWORD(bytes.size()),&read,nullptr)!=FALSE;CloseHandle(file);
    if(!ok||read!=bytes.size())return {};
    return bytes;
}
static bool audit_d3d9_device_slots() {
    // Expected DWORD stack slots (including this) in IDirect3DDevice9's d3d9.h order.
    static constexpr unsigned d3d9_h_arities[]={3,1,1,1,1,1,2,2,3,2,4,4,2,3,3,1,2,5,5,3,2,4,3,9,10,8,7,7,9,9,5,3,3,3,6,4,7,3,3,2,2,1,1,7,3,3,3,2,2,2,2,3,3,3,3,3,3,3,3,3,1,2,2,2,3,3,4,4,4,4,2,3,3,2,2,2,2,2,1,2,1,4,7,5,9,7,3,2,2,2,2,3,2,2,4,4,4,4,4,4,5,5,3,3,2,2,3,2,2,4,4,4,4,4,4,4,4,2,3};
    x86::Host host;
    x86::register_d3d9_com(host.com,host);
    for(const auto& [name,iface]:host.com.ifaces)if(iface.methods.size()!=iface.slots-3) {
        std::fprintf(stderr,"D3D9 slot audit: %s has %zu methods for %u slots\\n",name.c_str(),iface.methods.size(),iface.slots);
        return false;
    }
    const auto& device=host.com.by_name("IDirect3DDevice9");
    if(device.slots!=sizeof(d3d9_h_arities)/sizeof(d3d9_h_arities[0])||device.methods.size()!=device.slots-3) {
        std::fprintf(stderr,"D3D9 slot audit: slots=%u arities=%zu methods=%zu\n",device.slots,sizeof(d3d9_h_arities)/sizeof(d3d9_h_arities[0]),device.methods.size());
        return false;
    }
    for(unsigned slot=3;slot<device.slots;++slot) {
        auto method=device.methods.find(slot);
        if(method==device.methods.end()||method->second.argc!=d3d9_h_arities[slot]) {
            std::fprintf(stderr,"D3D9 slot audit: slot=%u expected argc=%u actual=%u\n",slot,d3d9_h_arities[slot],method==device.methods.end()?0:method->second.argc);
            return false;
        }
    }
    static constexpr unsigned surface_arities[]={3,1,1,2,5,4,2,2,1,1,1,3,2,4,1,2,2};
    static constexpr unsigned texture_arities[]={3,1,1,2,5,4,2,2,1,1,1,2,1,1,2,1,1,3,3,5,2,2};
    auto audit_interface=[&](const char* name,const unsigned* arities,size_t count) {
        const auto& iface=host.com.by_name(name);
        if(iface.slots!=count||iface.methods.size()!=iface.slots-3) {
            std::fprintf(stderr,"D3D9 slot audit: %s slots=%u arities=%zu methods=%zu\n",name,iface.slots,count,iface.methods.size());
            return false;
        }
        for(unsigned slot=3;slot<iface.slots;++slot) {
            auto method=iface.methods.find(slot);
            if(method==iface.methods.end()||method->second.argc!=arities[slot]) {
                std::fprintf(stderr,"D3D9 slot audit: %s slot=%u expected argc=%u actual=%u\n",name,slot,arities[slot],method==iface.methods.end()?0:method->second.argc);
                return false;
            }
        }
        return true;
    };
    if(!audit_interface("IDirect3DSurface9",surface_arities,sizeof(surface_arities)/sizeof(surface_arities[0]))||
       !audit_interface("IDirect3DTexture9",texture_arities,sizeof(texture_arities)/sizeof(texture_arities[0])))return false;
    return true;
}
static std::wstring wide(const char* value) {
    int count=MultiByteToWideChar(CP_UTF8,0,value,-1,nullptr,0);
    std::wstring result(count?size_t(count):0,L'\0');
    if(count)MultiByteToWideChar(CP_UTF8,0,value,-1,result.data(),count);
    if(!result.empty())result.pop_back();
    return result;
}
static LRESULT CALLBACK d3d9_test_window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    return DefWindowProcW(window,message,wparam,lparam);
}
int main(int argc,char** argv) {
    static_assert(x86::d3d9_up_index_count(D3DPT_POINTLIST,3)==3);
    static_assert(x86::d3d9_up_index_count(D3DPT_LINELIST,2)==4);
    static_assert(x86::d3d9_up_index_count(D3DPT_LINESTRIP,2)==3);
    static_assert(x86::d3d9_up_index_count(D3DPT_TRIANGLELIST,2)==6);
    static_assert(x86::d3d9_up_index_count(D3DPT_TRIANGLESTRIP,2)==4);
    static_assert(x86::d3d9_up_index_count(D3DPT_TRIANGLEFAN,2)==4);
    if(!audit_d3d9_device_slots()){std::fprintf(stderr,"X86-D3D9 FAIL: IDirect3DDevice9 slot/arity audit against d3d9.h\n");return 1;}
    if(argc!=4){std::fprintf(stderr,"usage: x86-d3d9 <guest.exe> <real-d3d9.dll> <d3dx9-donor-dir>\n");return 2;}
    auto file=read_file(argv[1]);
    if(file.empty()){std::fprintf(stderr,"X86-D3D9 FAIL: guest image empty\n");return 1;}
    HMODULE shim=LoadLibraryW(wide(argv[2]).c_str());
    if(!shim){std::fprintf(stderr,"X86-D3D9 FAIL: LoadLibrary(real shim) error=%lu\n",GetLastError());return 1;}
    using Create9=void* (WINAPI*)(UINT);
    using Create9Ex=HRESULT (WINAPI*)(UINT,void**);
    auto create9=reinterpret_cast<Create9>(GetProcAddress(shim,"Direct3DCreate9"));
    auto create9ex=reinterpret_cast<Create9Ex>(GetProcAddress(shim,"Direct3DCreate9Ex"));
    if(!create9||!create9ex){std::fprintf(stderr,"X86-D3D9 FAIL: real shim factory export missing\n");return 1;}
    const wchar_t class_name[]=L"X86D3D9BridgeTest";
    WNDCLASSW window_class{};window_class.lpfnWndProc=d3d9_test_window_proc;window_class.hInstance=GetModuleHandleW(nullptr);window_class.lpszClassName=class_name;
    RegisterClassW(&window_class);
    HWND window=CreateWindowW(class_name,L"x86 d3d9 bridge",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,window_class.hInstance,nullptr);
    if(!window){std::fprintf(stderr,"X86-D3D9 FAIL: CreateWindowW error=%lu\n",GetLastError());return 1;}
    const std::string donor_dir=argv[3];
    auto donor=[donor_dir](const std::string& name) { return read_file((donor_dir+"/"+name).c_str()); };
    auto game_byte=[](const std::string& name) { return name=="d3dx9_43.dll"?std::vector<uint8_t>{0x11}:std::vector<uint8_t>{}; };
    auto runtime_byte=[](const std::string& name) { return x86::is_d3dx9_dll(name)?std::vector<uint8_t>{0x22}:std::vector<uint8_t>{}; };
    auto fallback_byte=[](const std::string& name) { return name=="other.dll"?std::vector<uint8_t>{0x33}:std::vector<uint8_t>{}; };
    auto precedence=x86::make_d3dx9_source(game_byte,runtime_byte,fallback_byte);
    if(precedence("d3dx9_43.dll")!=std::vector<uint8_t>{0x11}||precedence("d3dx9_42.dll")!=std::vector<uint8_t>{0x22}||precedence("other.dll")!=std::vector<uint8_t>{0x33}) {
        std::fprintf(stderr,"X86-D3D9 FAIL: D3DX9 source precedence\n");return 1;
    }
    std::vector<std::string> donor_gaps;
    auto observe=[&](const std::string& dll,const std::vector<std::string>& unresolved) {
        if(!x86::is_d3dx9_donor_dll(dll))return;
        donor_gaps.insert(donor_gaps.end(),unresolved.begin(),unresolved.end());
    };
    std::string d3d9_gap_log;
    std::string d3d9_route_log;
    auto result=x86::run_image(file,"x86-d3d9-guest.exe","x86-d3d9-guest.exe",nullptr,nullptr,100'000'000,
        x86::make_d3dx9_source(nullptr,donor,nullptr),false,
        nullptr,observe,[&](x86::Host& host) {
            host.tee=[&](const std::string& line){d3d9_gap_log+=line+"\n";};
            host.route_trace=[&](const std::string& line){d3d9_route_log+=line+"\n";};
            host.d3d9_factory=[=](bool ex,uint32_t version,uintptr_t* out)->uintptr_t {
                if(!out)return uint32_t(E_POINTER);
                *out=0;
                if(ex){void* object=nullptr;HRESULT hr=create9ex(version,&object);*out=reinterpret_cast<uintptr_t>(object);return uint32_t(hr);}
                *out=reinterpret_cast<uintptr_t>(create9(version));return 0;
            };
            host.d3d9_window=[window](uint32_t hwnd)->uintptr_t{return hwnd==1?reinterpret_cast<uintptr_t>(window):0;};
        });
    DestroyWindow(window);
    FreeLibrary(shim);
    for(const auto& gap:donor_gaps)std::fprintf(stderr,"X86-D3D9 donor unresolved: %s\n",gap.c_str());
    if(!result.loaded||!result.finished||result.exit_code!=0){
        std::fprintf(stderr,"X86-D3D9 FAIL: loaded=%d finished=%d exit=0x%08x fault=%s\n%s%s",result.loaded,result.finished,result.exit_code,result.fault.c_str(),d3d9_route_log.c_str(),d3d9_gap_log.c_str());
        return 1;
    }
    if(!donor_gaps.empty()){
        std::fprintf(stderr,"X86-D3D9 FAIL: D3DX9 donor has unresolved imports\n");
        return 1;
    }
    if(d3d9_gap_log.find("x86 D3D9 gap: IDirect3DSurface9::GetDC unsupported")==std::string::npos){
        std::fprintf(stderr,"X86-D3D9 FAIL: unbridged COM method did not report a gap\n");
        return 1;
    }
    std::puts("X86-D3D9 PASS: real x64 shim created, rendered indexed textured quad with alpha blending, presented, read back two pixels; unbridged COM call logged a gap");
    return 0;
}
