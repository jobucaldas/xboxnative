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
    if(argc!=3){std::fprintf(stderr,"usage: x86-d3d9 <guest.exe> <real-d3d9.dll>\n");return 2;}
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
    std::string d3d9_gap_log;
    auto result=x86::run_image(file,"x86-d3d9-guest.exe","x86-d3d9-guest.exe",nullptr,nullptr,100'000'000,nullptr,false,
        nullptr,nullptr,[&](x86::Host& host) {
            host.tee=[&](const std::string& line){d3d9_gap_log+=line+"\n";};
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
    if(!result.loaded||!result.finished||result.exit_code!=0){
        std::fprintf(stderr,"X86-D3D9 FAIL: loaded=%d finished=%d exit=0x%08x fault=%s\n",result.loaded,result.finished,result.exit_code,result.fault.c_str());
        return 1;
    }
    if(d3d9_gap_log.find("x86 D3D9 gap: IDirect3DDevice9::slot 4")==std::string::npos){
        std::fprintf(stderr,"X86-D3D9 FAIL: unbridged COM method did not report a gap\n");
        return 1;
    }
    std::puts("X86-D3D9 PASS: real x64 shim created, cleared, drew, presented and read back the target pixel; unbridged COM call logged a gap");
    return 0;
}
