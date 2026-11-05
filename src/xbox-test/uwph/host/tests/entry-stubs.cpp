// Off-device behavioral checks for error domains, outputs, and log-once behavior.
#include <windows.h>
#include <mmsystem.h>
#include <imm.h>
#include <shlobj.h>
#include <cstdio>
static unsigned calls;
static void logger(const char*){++calls;}
int main(int argc,char** argv) {
 if(argc!=2)return 2;
 HMODULE h=LoadLibraryA(argv[1]); if(!h)return 3;
 auto resolve=(UINT_PTR(*)(const char*,const char*))GetProcAddress(h,"entry_stubs_resolve");
 auto set=(void(*)(void(*)(const char*)))GetProcAddress(h,"entry_stubs_set_log");
 if(!resolve||!set)return 4;set(logger);
 unsigned failures=0;
 #define CHECK(x) do{if(!(x)){printf("FAIL %s\n",#x);++failures;}}while(0)
 auto devices=(decltype(&waveOutGetNumDevs))resolve("winmm.dll","waveOutGetNumDevs");
 CHECK(devices && devices()==0); CHECK(devices()==0); CHECK(calls==1);
 auto open=(decltype(&waveOutOpen))resolve("winmm.dll","waveOutOpen");
 HWAVEOUT handle=(HWAVEOUT)1;
 CHECK(open(&handle,WAVE_MAPPER,nullptr,0,0,0)==MMSYSERR_BADDEVICEID);CHECK(handle==nullptr);
 auto known=(decltype(&SHGetKnownFolderPath))resolve("shell32.dll","SHGetKnownFolderPath");
 PWSTR path=(PWSTR)1; GUID id{};CHECK(known(id,0,nullptr,&path)==E_NOTIMPL); CHECK(path==nullptr);
 auto composition=(decltype(&ImmGetCompositionStringW))resolve("imm32.dll","ImmGetCompositionStringW");
 CHECK(composition(nullptr,0,nullptr,0)==IMM_ERROR_NODATA);
 auto raw=(decltype(&GetRawInputDeviceList))resolve("user32.dll","GetRawInputDeviceList");
 UINT count=99; CHECK(raw(nullptr,&count,sizeof(RAWINPUTDEVICELIST))==0);CHECK(count==0);
 auto tick=(decltype(&timeGetTime))resolve("winmm.dll","timeGetTime");
 DWORD before=tick();Sleep(100);DWORD after=tick(); printf("timer before=%lu after=%lu native=%lu\n",before,after,(DWORD)GetTickCount64()); CHECK(after==DWORD(GetTickCount64()) || DWORD(GetTickCount64()-after)<=16); CHECK(DWORD(after-before)>=50);
 CHECK(resolve("user32.dll","UnknownEntry")==0);
 printf("ENTRY-STUB-TEST failures=%u (OFF-DEVICE)\n",failures); return failures?1:0;
}
