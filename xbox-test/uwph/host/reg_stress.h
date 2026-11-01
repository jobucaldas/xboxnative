#pragma once
// Kill-mid-write consistency test for the SQLite-backed registry (reg_builtin.h).
// verify(): after a crash/kill, the db must open, pass PRAGMA integrity_check, and satisfy the
// invariants of the writer below. run(): verify, repair, then write forever (until killed).
//  Writer step i (i = 0,1,2,..):  RegCreateKeyExW HKCU\Software\RegStress\g<i>\a\b   (4 key rows, ONE txn)
//                                 RegSetValueExW  HKCU\Software\RegStress  "n" = i      (separate txn)
//  Invariants after any kill:  every key's parent exists; g0..gM contiguous; n is M or M-1 (killed between the two txns).
#include "reg_builtin.h"
namespace regstress {
static void say(const char* fmt, ...) {
    char b[300]; va_list a; va_start(a,fmt); vsnprintf(b,sizeof b,fmt,a); va_end(a); regb::note(b);
}
struct Verdict { bool ok=true; long M=-1; long n=-2; size_t keys=0; std::string integrity; };
static Verdict verify() {
    Verdict v; std::lock_guard<std::mutex> g(regb::lock); regb::ensure();
    v.integrity=regb::db_ok?regb::pragma_text("PRAGMA integrity_check"):"no-db";
    if(v.integrity!="ok") v.ok=false;
    v.keys=regb::keys.size();
    for(auto& kv:regb::keys) {
        size_t p=kv.first.find_last_of(L'\\'); if(p==std::wstring::npos) continue;
        std::wstring parent=kv.first.substr(0,p);
        if(!regb::is_root(parent) && !regb::keys.count(parent)) { v.ok=false; say("REGSTRESS VIOLATION orphan key %s",regb::utf8(kv.first).c_str()); }
    }
    std::wstring base=L"hkcu\\software\\regstress\\g";
    for(;;) { long i=v.M+1; std::wstring k=base+std::to_wstring(i); if(regb::keys.count(k)&&regb::keys.count(k+L"\\a")&&regb::keys.count(k+L"\\a\\b")) v.M=i; else break; }
    for(auto& kv:regb::keys) if(kv.first.compare(0,base.size(),base)==0) {   // anything beyond the contiguous prefix is a violation
        long i=wcstol(kv.first.c_str()+base.size(),nullptr,10); if(i>v.M) { v.ok=false; say("REGSTRESS VIOLATION key beyond prefix %s",regb::utf8(kv.first).c_str()); }
    }
    auto it=regb::keys.find(L"hkcu\\software\\regstress");
    if(it!=regb::keys.end()) { auto vi=it->second.values.find(L"n"); if(vi!=it->second.values.end() && vi->second.data.size()==4) v.n=*(const DWORD*)vi->second.data.data(); }
    if(v.M>=0 && !(v.n==v.M || v.n==v.M-1)) { v.ok=false; say("REGSTRESS VIOLATION n=%ld M=%ld",v.n,v.M); }
    if(v.M<0 && v.n>=0) { v.ok=false; say("REGSTRESS VIOLATION n=%ld but no keys",v.n); }
    say("REGSTRESS-VERIFY %s integrity=%s M=%ld n=%ld keys=%u",v.ok?"OK":"BAD",v.integrity.c_str(),v.M,v.n,(unsigned)v.keys);
    return v;
}
static int run(unsigned seconds /*0 = until killed*/) {
    Verdict v=verify(); long i=v.M+1;
    if(v.M>=0 && v.n!=v.M) { HKEY k=0; DWORD d=(DWORD)v.M; regb::RegCreateKeyW(HKEY_CURRENT_USER,L"Software\\RegStress",&k); regb::RegSetValueExW(k,L"n",0,REG_DWORD,(const BYTE*)&d,4); regb::RegCloseKey(k); }
    ULONGLONG t0=GetTickCount64();
    HKEY root=0; regb::RegCreateKeyW(HKEY_CURRENT_USER,L"Software\\RegStress",&root);
    for(;;++i) {
        HKEY k=0; std::wstring sub=L"Software\\RegStress\\g"+std::to_wstring(i)+L"\\a\\b";
        if(regb::RegCreateKeyExW(HKEY_CURRENT_USER,sub.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&k,nullptr)==0) regb::RegCloseKey(k);
        DWORD d=(DWORD)i; regb::RegSetValueExW(root,L"n",0,REG_DWORD,(const BYTE*)&d,4);
        if(i%25==0) say("REGSTRESS writing i=%ld (committed)",i);
        if(seconds && GetTickCount64()-t0>seconds*1000ull) { say("REGSTRESS finished cleanly at i=%ld",i); return v.ok?0:1; }
    }
}
}
