// Off-device: runs reg_test.c (REG_DIRECT) against the regb builtins, then proves the data
// survives a reload from registry.json. Build + run: see run-reg-selftest.sh (Wine).
#include <cstdio>
#include "reg_builtin.h"
#include "reg_stress.h"
#include "../../shared/vulkan_driver_registry.h"
static std::string utf8_of(const std::wstring& w) {
    int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);
    std::string s(n,'\0'); WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),&s[0],n,nullptr,nullptr); return s;
}
extern "C" {
LONG WINAPI regb_RegCreateKeyW(HKEY a, LPCWSTR b, PHKEY c) { return regb::RegCreateKeyW(a,b,c); }
LONG WINAPI regb_RegCreateKeyExW(HKEY a, LPCWSTR b, DWORD c, LPWSTR d, DWORD e, REGSAM f, const SECURITY_ATTRIBUTES* g, PHKEY h, LPDWORD i) { return regb::RegCreateKeyExW(a,b,c,d,e,f,g,h,i); }
LONG WINAPI regb_RegSetValueExA(HKEY a, LPCSTR b, DWORD c, DWORD d, const BYTE* e, DWORD f) { return regb::RegSetValueExA(a,b,c,d,e,f); }
LONG WINAPI regb_RegSetValueExW(HKEY a, LPCWSTR b, DWORD c, DWORD d, const BYTE* e, DWORD f) { return regb::RegSetValueExW(a,b,c,d,e,f); }
LONG WINAPI regb_RegQueryValueExA(HKEY a, LPCSTR b, LPDWORD c, LPDWORD d, LPBYTE e, LPDWORD f) { return regb::RegQueryValueExA(a,b,c,d,e,f); }
LONG WINAPI regb_RegQueryValueExW(HKEY a, LPCWSTR b, LPDWORD c, LPDWORD d, LPBYTE e, LPDWORD f) { return regb::RegQueryValueExW(a,b,c,d,e,f); }
LONG WINAPI regb_RegDeleteValueA(HKEY a, LPCSTR b) { return regb::RegDeleteValueA(a,b); }
LONG WINAPI regb_RegOpenKeyExW(HKEY a, LPCWSTR b, DWORD c, REGSAM d, PHKEY e) { return regb::RegOpenKeyExW(a,b,c,d,e); }
LONG WINAPI regb_RegCloseKey(HKEY a) { return regb::RegCloseKey(a); }
DWORD reg_entry(void);
void regb_dbg(const char* s) { puts(s); }
}
static int bad;
static void say(const char* s) { puts(s); }
#define EXPECT(c) do { if(!(c)) { printf("SELFTEST FAIL line %d: %s\n",__LINE__,#c); bad++; } } while(0)
int main(int argc,char** argv) {
    std::wstring dir=L"."; if(argc>1) { dir.assign(argv[1],argv[1]+strlen(argv[1])); }
    std::string mode=argc>2?argv[2]:"";
    if(mode=="stress") { regb::init(dir,say); return regstress::run(argc>3?atoi(argv[3]):0); }          // killed externally; see run-reg-selftest.sh
    if(mode=="verify") { regb::init(dir,say); return regstress::verify().ok?0:1; }
    for(const wchar_t* f:{L"\\registry.json",L"\\registry.json.imported",L"\\registry.sqlite",L"\\registry.sqlite-wal",L"\\registry.sqlite-shm"}) DeleteFileW((dir+f).c_str());
    // one-time import: a legacy uwph-registry/1 file (one key with a DWORD, one binary, escaped name) is imported, then renamed
    { FILE* f=_wfopen((dir+L"\\registry.json").c_str(),L"wb");
      fputs("{\"format\":\"uwph-registry/1\",\"keys\":[\n{\"path\":\"HKCU\\\\Software\\\\Legacy\\\\App\",\"values\":[{\"name\":\"Vol\",\"type\":4,\"hex\":\"07000000\"},{\"name\":\"q\\\"x\",\"type\":3,\"hex\":\"00ff10\"}]},\n{\"path\":\"HKCU\\\\Software\",\"values\":[]}]}\n",f); fclose(f); }
    regb::init(dir,say);
    { HKEY k=0; BYTE b[8]; DWORD t=0,n=sizeof b;
      EXPECT(regb::RegOpenKeyExW(HKEY_CURRENT_USER,L"SOFTWARE\\legacy\\APP",0,KEY_READ,&k)==0);
      EXPECT(regb::RegQueryValueExW(k,L"vol",0,&t,b,&n)==0 && t==REG_DWORD && n==4 && *(DWORD*)b==7);
      n=sizeof b; EXPECT(regb::RegQueryValueExW(k,L"Q\"X",0,&t,b,&n)==0 && t==REG_BINARY && n==3 && b[0]==0 && b[1]==0xff && b[2]==0x10);
      regb::RegCloseKey(k);
      EXPECT(GetFileAttributesW((dir+L"\\registry.json").c_str())==INVALID_FILE_ATTRIBUTES && GetFileAttributesW((dir+L"\\registry.json.imported").c_str())!=INVALID_FILE_ATTRIBUTES); }
    // a stale registry.json put back after the import must NOT be re-imported (user_version=1)
    { FILE* f=_wfopen((dir+L"\\registry.json").c_str(),L"wb"); fputs("{\"keys\":[{\"path\":\"HKCU\\\\Zombie\",\"values\":[]}]}",f); fclose(f); }
    regb::init(dir,say);
    { HKEY k=0; EXPECT(regb::RegOpenKeyExW(HKEY_CURRENT_USER,L"Zombie",0,KEY_READ,&k)==ERROR_FILE_NOT_FOUND);
      EXPECT(regb::RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Legacy\\App",0,KEY_READ,&k)==0); regb::RegCloseKey(k); }
    DeleteFileW((dir+L"\\registry.json").c_str());
    regb::init(dir,say);
    // run twice: second run sees existing key/values (idempotent, like a console relaunch)
    for(int pass=0;pass<2;pass++) { printf("--- pass %d\n",pass+1); EXPECT(reg_entry()==42); }
    // reload from disk: values written by the guest must be there, deleted value must stay deleted
    regb::init(dir,say);
    HKEY k=0; BYTE b[64]; DWORD t=0,n=sizeof b;
    EXPECT(regb::RegOpenKeyExW(HKEY_CURRENT_USER,L"software\\VED\\megabonk",0,KEY_READ,&k)==0);   // case-insensitive
    EXPECT(regb::RegQueryValueExW(k,L"volume_H1234",0,&t,b,&n)==0 && t==REG_DWORD && n==4 && *(DWORD*)b==7);
    n=sizeof b; EXPECT(regb::RegQueryValueExA(k,"Name_h5678",0,&t,b,&n)==0 && t==REG_SZ);       // ANSI view of a UTF-16 string
    n=sizeof b; EXPECT(regb::RegQueryValueExA(k,"Ansi_h0def",0,&t,b,&n)==ERROR_FILE_NOT_FOUND);
    n=0; EXPECT(regb::RegQueryValueExA(k,"Blob_h9abc",0,&t,nullptr,&n)==0 && n==8 && t==REG_BINARY);
    EXPECT(regb::RegCloseKey(k)==0); EXPECT(regb::RegCloseKey(k)==ERROR_INVALID_HANDLE);
    EXPECT(regb::RegOpenKeyExW((HKEY)0x1234,L"x",0,KEY_READ,&k)==ERROR_INVALID_HANDLE);
    EXPECT(regb::RegCreateKeyW(HKEY_CURRENT_USER,L"\\bad",&k)==ERROR_BAD_PATHNAME);
    EXPECT(regb::RegCreateKeyW(HKEY_CURRENT_USER,L"a",nullptr)==ERROR_INVALID_PARAMETER);
    // Persistence failures must not report success or mutate the cached view.
    EXPECT(regb::RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\VED\\megabonk",0,KEY_READ,&k)==0);
    EXPECT(regb::sql("PRAGMA query_only=ON"));
    DWORD replacement=99;
    EXPECT(regb::RegSetValueExW(k,L"volume_H1234",0,REG_DWORD,(const BYTE*)&replacement,sizeof replacement)==ERROR_WRITE_FAULT);
    EXPECT(regb::RegDeleteValueA(k,"volume_H1234")==ERROR_WRITE_FAULT);
    HKEY rejected=0;
    EXPECT(regb::RegCreateKeyW(HKEY_CURRENT_USER,L"WriteFailure\\Child",&rejected)==ERROR_WRITE_FAULT);
    EXPECT(regb::RegOpenKeyExW(HKEY_CURRENT_USER,L"WriteFailure",0,KEY_READ,&rejected)==ERROR_FILE_NOT_FOUND);
    n=sizeof b; EXPECT(regb::RegQueryValueExW(k,L"volume_H1234",0,&t,b,&n)==0 && n==4 && *(DWORD*)b==7);
    EXPECT(regb::RegCloseKey(k)==0);
    regb::init(dir,say);
    EXPECT(regb::RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\VED\\megabonk",0,KEY_READ,&k)==0);
    n=sizeof b; EXPECT(regb::RegQueryValueExW(k,L"volume_H1234",0,&t,b,&n)==0 && n==4 && *(DWORD*)b==7);
    EXPECT(regb::RegCloseKey(k)==0);
    // An unavailable backing database must not silently become a volatile store.
    regb::init(dir+L"\\missing-parent\\registry",say);
    EXPECT(regb::RegCreateKeyW(HKEY_CURRENT_USER,L"MustPersist",&rejected)==ERROR_WRITE_FAULT);
    EXPECT(regb::RegOpenKeyExW(HKEY_CURRENT_USER,L"MustPersist",0,KEY_READ,&rejected)==ERROR_FILE_NOT_FOUND);
    // Enumeration: the Khronos Vulkan loader reads HKLM\SOFTWARE\Khronos\Vulkan\Drivers with RegEnumValueA and stops at
    // ERROR_NO_MORE_ITEMS. Cover the ANSI name, the UTF-16 value behind it, the short-buffer answers and the end.
    regb::init(dir,say);
    { HKEY vk=0; DWORD driverValue=99;
      EXPECT(regb::RegCreateKeyW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Khronos\\Vulkan\\Drivers",&vk)==0);
      const wchar_t* manifest=L"C:\\runtime\\vulkan-null-icd.json";
      EXPECT(regb::RegSetValueExW(vk,manifest,0,REG_DWORD,(const BYTE*)&driverValue,sizeof driverValue)==0);
      char name[256]; DWORD nameLen=sizeof name, type=0, data=0, dataLen=sizeof data;
      EXPECT(regb::RegEnumValueA(vk,0,name,&nameLen,nullptr,&type,(LPBYTE)&data,&dataLen)==0);
      EXPECT(!strcmp(name,"C:\\runtime\\vulkan-null-icd.json") && type==REG_DWORD && dataLen==4 && data==99);
      // A name buffer that is too small fails the call and reports the size it needs (including the terminator).
      nameLen=4; dataLen=sizeof data;
      EXPECT(regb::RegEnumValueA(vk,0,name,&nameLen,nullptr,&type,(LPBYTE)&data,&dataLen)==ERROR_MORE_DATA && nameLen==32);
      nameLen=sizeof name; dataLen=0;   // count-only data query
      EXPECT(regb::RegEnumValueA(vk,0,name,&nameLen,nullptr,&type,nullptr,&dataLen)==0 && dataLen==4);
      nameLen=sizeof name; dataLen=sizeof data;
      EXPECT(regb::RegEnumValueA(vk,1,name,&nameLen,nullptr,&type,(LPBYTE)&data,&dataLen)==ERROR_NO_MORE_ITEMS);
      wchar_t wname[256]; DWORD wlen=256; dataLen=sizeof data;
      EXPECT(regb::RegEnumValueW(vk,0,wname,&wlen,nullptr,&type,(LPBYTE)&data,&dataLen)==0 && !wcscmp(wname,manifest) && wlen==31);
      EXPECT(regb::RegCloseKey(vk)==0); }
    // The value survives a reload, so the next launch's enumeration sees the same driver entry.
    regb::init(dir,say);
    { HKEY vk=0; char name[256]; DWORD nameLen=sizeof name, type=0, data=0, dataLen=sizeof data;
      EXPECT(regb::RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Khronos\\Vulkan\\Drivers",0,KEY_READ,&vk)==0);
      EXPECT(regb::RegEnumValueA(vk,0,name,&nameLen,nullptr,&type,(LPBYTE)&data,&dataLen)==0 && data==99);
      EXPECT(regb::RegCloseKey(vk)==0); }
    // Registration replaces, it does not append. The key survives an upgrade and the loader de-duplicates it by
    // manifest basename keeping the first entry it enumerates, so an entry left by an earlier install would shadow the
    // live manifest. Seed that history — two installs' worth of our own manifests, a third-party driver and a manifest
    // that no longer exists — and check the prune leaves exactly the live pair plus the third-party driver.
    regb::init(dir,say);
    { HKEY vk=0; DWORD zero=0;
      const std::wstring live=dir+L"\\runtime", old=dir+L"\\old-install", older=dir+L"\\older-install";
      EXPECT(regb::RegCreateKeyW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Khronos\\Vulkan\\Drivers",&vk)==0);
      for(auto& manifest:{live+L"\\vulkan-dzn.json",live+L"\\vulkan-null-icd.json",
                          old+L"\\vulkan-dzn.json",old+L"\\vulkan-null-icd.json",
                          older+L"\\vulkan-dzn.json"})
          EXPECT(regb::RegSetValueExW(vk,manifest.c_str(),0,REG_DWORD,(const BYTE*)&zero,sizeof zero)==0);
      // A third-party driver whose manifest is still on disk must survive; one whose file is gone must not.
      std::wstring thirdParty=dir+L"\\third-party\\vulkan-icd.json", gone=dir+L"\\gone\\vulkan-icd.json";
      CreateDirectoryW((dir+L"\\third-party").c_str(),nullptr);
      { FILE* f=_wfopen(thirdParty.c_str(),L"wb"); if(f) fclose(f); }
      EXPECT(regb::RegSetValueExW(vk,thirdParty.c_str(),0,REG_DWORD,(const BYTE*)&zero,sizeof zero)==0);
      EXPECT(regb::RegSetValueExW(vk,gone.c_str(),0,REG_DWORD,(const BYTE*)&zero,sizeof zero)==0);
      const unsigned removed=vulkan_driver_registry::prune_stale(vk,live,{L"vulkan-dzn.json",L"vulkan-null-icd.json"});
      // Five: two stale dzn, one stale null ICD, the C:\runtime\vulkan-null-icd.json left by the block above
      // (an install on another drive), and the manifest whose file is gone.
      EXPECT(removed==5);
      EXPECT(regb::RegCloseKey(vk)==0); }
    // Reload from disk: the prune is a persistent change, and the loader's own enumeration (ANSI, ordered by name)
    // must now see the live dzn ICD first with the null ICD as the fallback.
    regb::init(dir,say);
    { HKEY vk=0; char name[1024]; DWORD nameLen=sizeof name, type=0, data=0, dataLen=sizeof data;
      EXPECT(regb::RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Khronos\\Vulkan\\Drivers",0,KEY_READ,&vk)==0);
      std::vector<std::string> seen;
      for(DWORD i=0;;++i) {
          nameLen=sizeof name; dataLen=sizeof data;
          if(regb::RegEnumValueA(vk,i,name,&nameLen,nullptr,&type,(LPBYTE)&data,&dataLen)!=0) break;
          seen.push_back(name);
      }
      EXPECT(seen.size()==3);
      EXPECT(seen.size()==3 && seen[0]==utf8_of(dir)+"\\runtime\\vulkan-dzn.json");
      EXPECT(seen.size()==3 && seen[1]==utf8_of(dir)+"\\runtime\\vulkan-null-icd.json");
      EXPECT(seen.size()==3 && seen[2]==utf8_of(dir)+"\\third-party\\vulkan-icd.json");
      EXPECT(regb::RegCloseKey(vk)==0); }
    printf(bad?"SELFTEST FAILED %d\n":"SELFTEST OK\n",bad);
    return bad!=0;
}
