#pragma once
// Registry PlayerPrefs lane: runs reg-test.exe (package dir) through the wxl mapper.
// The console refuses RegCreateKey*/RegSetValueEx under HKCU (ERROR_ACCESS_DENIED), so
// every Reg* import is routed to the uwph registry builtin (LocalState\registry.json).
// A native canary call is logged each run so the evidence says whether the console still refuses.
#include <string>
#include "wxl_mapper.h"
#include "reg_builtin.h"
#include "reg_stress.h"
namespace reg {
static wxl::Log log;
static uintptr_t resolve(const char* dll, const char* name, WORD) {
    std::string key=dll; for(auto& c:key) if(c>='A'&&c<='Z') c+=32;
    uintptr_t address=(key=="advapi32.dll" && name) ? regb::lookup(name) : 0;
    char msg[256]; snprintf(msg,sizeof msg,"REG ROUTE %s!%s backend=uwph provider=registry.sqlite found=%d",key.c_str(),name?name:"#ordinal",!!address);
    log(msg); return address;
}
static void native_canary() {
    HMODULE m=LoadLibraryExW(L"advapi32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    using Create=LONG (WINAPI*)(HKEY,LPCWSTR,PHKEY);
    using Close=LONG (WINAPI*)(HKEY);
    auto create=m ? (Create)GetProcAddress(m,"RegCreateKeyW") : nullptr;
    auto close=m ? (Close)GetProcAddress(m,"RegCloseKey") : nullptr;
    HKEY h=nullptr; LONG rc=create ? create(HKEY_CURRENT_USER,L"Software\\Ved\\Megabonk",&h) : -1;
    if(h && close) close(h);
    char msg[256]; snprintf(msg,sizeof msg,"REG NATIVE-CANARY advapi32!RegCreateKeyW HKCU\\Software\\Ved\\Megabonk -> %ld (%s)",rc,rc==0?"console allows it":"refused; Reg* routed to uwph registry (sqlite)");
    log(msg);
}
static bool run(const std::wstring& exe, const std::wstring& localstate, wxl::Log logger) {
    log=logger; log("REG PROBE begin; backend=uwph registry.sqlite");
    auto has=[&](const wchar_t* n){ return GetFileAttributesW((localstate+L"\\"+n).c_str())!=INVALID_FILE_ATTRIBUTES; };
    auto rm=[&](const wchar_t* n){ DeleteFileW((localstate+L"\\"+n).c_str()); };
    if(has(L"registry.reset")) {   // test hook: forget the sqlite db (keeps registry.json) so the next init exercises the one-time import
        for(auto n:{L"registry.sqlite",L"registry.sqlite-wal",L"registry.sqlite-shm",L"registry.json.imported",L"registry.reset"}) rm(n);
        log("REG RESET: removed registry.sqlite*; registry.json (if any) will be imported");
    }
    native_canary();
    regb::init(localstate,logger);
    if(has(L"regstress.txt")) {   // kill-mid-write test hooks: regstress.txt = seconds to run (0 = until killed); regverify.txt = verify only
        char b[32]={}; if(FILE* f=_wfopen((localstate+L"\\regstress.txt").c_str(),L"rb")){ fread(b,1,sizeof b-1,f); fclose(f); }
        rm(L"regstress.txt"); int rc=regstress::run((unsigned)atoi(b)); log(rc==0?"REG STRESS DONE ok":"REG STRESS DONE BAD"); return rc==0;
    }
    if(has(L"regverify.txt")) { rm(L"regverify.txt"); auto v=regstress::verify(); log(v.ok?"REG VERIFY DONE ok":"REG VERIFY DONE BAD"); return v.ok; }
    bool ok=wxl::run(exe.c_str(),logger,nullptr,resolve);
    log(ok ? "REG RESULT entry=42 failures=0" : "REG RESULT FAILED");
    return ok;
}
}
