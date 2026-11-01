// Off-device: runs reg_test.c (REG_DIRECT) against the regb builtins, then proves the data
// survives a reload from registry.json. Build + run: see run-reg-selftest.sh (Wine).
#include <cstdio>
#include "reg_builtin.h"
#include "reg_stress.h"
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
    printf(bad?"SELFTEST FAILED %d\n":"SELFTEST OK\n",bad);
    return bad!=0;
}
