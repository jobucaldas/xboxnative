#include <windows.h>
// Genuine static Microsoft CRT throw/catch, inside a manually mapped DLL.
// No custom throw adapter; the native CRT must obtain the actual image base.
static int destructors;
struct Guard {~Guard(){++destructors;}};
__declspec(noinline) static void raiseValue(){Guard guard;throw 73;}
extern "C" __declspec(dllexport) int native_cxx_test(){
 using Lookup=PVOID(WINAPI*)(PVOID,PVOID*);
 auto lookup=(Lookup)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"RtlPcToFileHeader");
 if(!lookup)return 3;
 HMODULE self=GetModuleHandleW(L"play-cxx.dll");PVOID base=nullptr;
 if(!self||lookup((PVOID)&native_cxx_test,&base)!=self||base!=self)return 4;
 auto dos=(IMAGE_DOS_HEADER*)self;
 auto nt=(IMAGE_NT_HEADERS*)((BYTE*)self+dos->e_lfanew);
 if(lookup((BYTE*)self+nt->OptionalHeader.SizeOfImage-1,&base)!=self||base!=self)return 5;
 base=self;if(lookup((PVOID)1,&base)||base)return 6;
 // Native code still uses the OS loader, rather than a mapped guest image.
 HMODULE native=nullptr;auto address=(PVOID)&GetCurrentProcessId;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
     (LPCWSTR)address,&native)||lookup(address,&base)!=native||base!=native)return 7;
 destructors=0;
 try {raiseValue();}catch(int value){return value==73&&destructors==1?42:1;}
 return 2;
}
