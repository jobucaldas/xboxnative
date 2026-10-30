// Off-device only: desktop substitutions validate host logic, not Xbox APIs.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
enum Failure { NONE, UNWIND, FREE_MEM, FREE_TLS, CLEAR_TLS, READ_TLS };
static Failure injected=NONE;
static BOOL WINAPI test_delete(PRUNTIME_FUNCTION p) { if (injected==UNWIND) { injected=NONE; return FALSE; } return RtlDeleteFunctionTable(p); }
static BOOL WINAPI test_free(LPVOID p,SIZE_T n,DWORD t) { if (injected==FREE_MEM) { injected=NONE; return FALSE; } return VirtualFree(p,n,t); }
static BOOL WINAPI test_tls_free(DWORD k) { if (injected==FREE_TLS) { injected=NONE; return FALSE; } return TlsFree(k); }
static BOOL WINAPI test_tls_set(DWORD k,LPVOID v) { if (!v && injected==CLEAR_TLS) { injected=NONE; return FALSE; } return TlsSetValue(k,v); }
static LPVOID WINAPI test_tls_get(DWORD k) { if (injected==READ_TLS) { injected=NONE; SetLastError(ERROR_INVALID_PARAMETER); return nullptr; } return TlsGetValue(k); }
#define TlsGetValue test_tls_get
#define RtlDeleteFunctionTable test_delete
#define VirtualFree test_free
#define TlsFree test_tls_free
#define TlsSetValue test_tls_set
#define VirtualAllocFromApp VirtualAlloc
#define VirtualProtectFromApp VirtualProtect
#include "../wxl_mapper.h"
static std::vector<std::string> events;
static void log_line(const char* s) { puts(s); events.emplace_back(s); }
static int find(const char* s) {
    for (size_t i=0;i<events.size();++i) if (events[i]==s) return (int)i;
    return -1;
}
static int count(const char* s) {
    int n=0; for (auto& e:events) if (e==s) ++n; return n;
}
static bool sequence(std::initializer_list<const char*> expected) {
    std::vector<std::string> actual;
    for (auto& e:events) if (e.rfind("TLS callback ",0)==0 || e.rfind("TLS DllMain ",0)==0) actual.push_back(e);
    std::vector<std::string> want; for (auto s:expected) want.emplace_back(s);
    return actual==want;
}
static BYTE carrier_block[64];
static DWORD carrier_index() { return 0; }
static BYTE* carrier_data() { return carrier_block; }
static bool path(const char* s,wchar_t* out) { return MultiByteToWideChar(CP_UTF8,0,s,-1,out,1024)!=0; }
int main(int argc,char** argv) {
    if (argc!=5) return 2;
    wchar_t good[1024],bad[1024],tls[1024],tls_bad[1024]; if (!path(argv[1],good)||!path(argv[2],bad)||!path(argv[3],tls)||!path(argv[4],tls_bad)) return 2;
    {
        wxl::Image image;
        if (!wxl::load(image,good,log_line) || !image.base || !image.attached) return 1;
        DWORD64 base=0; auto entry=(DWORD64)image.dllEntry;
        if (!RtlLookupFunctionEntry(entry,&base,nullptr) || base!=(DWORD64)image.base) return 1;
        if (wxl::load(image,good,log_line)) return 1;
        if (!image.close() || !image.close() || image.base) return 1;
        if (RtlLookupFunctionEntry(entry,&base,nullptr)) return 1;
        if (count("LIFECYCLE attach")!=1 || count("LIFECYCLE detach")!=1 ||
            find("LIFECYCLE unwind lookup ok")<0 || find("LIFECYCLE detach")>find("WXL RtlDeleteFunctionTable ok=1")) return 1;
    }
    if (count("LIFECYCLE detach")!=1) return 1;
    events.clear();
    {
        wxl::Image image;
        if (wxl::load(image,bad,log_line) || image.base || image.functions || image.attached) return 1;
        if (count("LIFECYCLE attach")!=1 || count("LIFECYCLE detach")!=0 ||
            find("WXL FAIL DllMain process attach")<0 ||
            find("WXL RtlDeleteFunctionTable ok=1")<find("WXL FAIL DllMain process attach")) return 1;
        if (!image.close()) return 1;
    }
    if (count("LIFECYCLE detach")!=0 || count("WXL RtlDeleteFunctionTable ok=1")!=1) return 1;
    for (Failure f : {UNWIND, FREE_MEM}) {
        events.clear(); wxl::Image image;
        if (!wxl::load(image,good,log_line)) return 1;
        auto base=image.base; injected=f;
        if (image.close() || image.base!=base || image.attached) return 1;
        if (f==UNWIND && !image.functions) return 1;
        if (f==FREE_MEM && image.functions) return 1;
        if (!image.close() || image.base || count("LIFECYCLE detach")!=1) return 1;
    }
    for (Failure f : {FREE_TLS, CLEAR_TLS}) {
        events.clear(); wxl::Image image; image.log=log_line;
        image.tlsKey=TlsAlloc(); if (image.tlsKey==TLS_OUT_OF_INDEXES) return 1;
        BYTE block=0; if (!TlsSetValue(image.tlsKey,&block)) return 1;
        image.tlsAttached=true; auto key=image.tlsKey; injected=f;
        if (image.close() || image.tlsKey!=key) return 1;
        if (!image.close() || image.tlsKey!=TLS_OUT_OF_INDEXES) return 1;
    }
    events.clear(); injected=UNWIND;
    if (wxl::run(good,log_line) || count("LIFECYCLE detach")!=1 ||
        find("WXL FAIL RtlDeleteFunctionTable")<0) return 1;
    events.clear(); injected=FREE_MEM;
    if (wxl::run(good,log_line) || count("LIFECYCLE detach")!=1 ||
        find("WXL FAIL VirtualFree")<0) return 1;
    wxl::Carrier carrier{carrier_index,carrier_data,sizeof carrier_block};
    events.clear();
    {
        wxl::Image image;
        if (!wxl::load(image,tls,log_line,carrier)) return 1;
        if (!image.close() || !image.close()) return 1;
    }
    if (!sequence({"TLS callback attach","TLS DllMain attach","TLS DllMain detach","TLS callback detach"})) return 1;
    events.clear();
    {
        wxl::Image image;
        if (!wxl::load(image,tls,log_line,carrier)) return 1;
        injected=CLEAR_TLS;
        if (image.close() || !image.close()) return 1;
    }
    if (!sequence({"TLS callback attach","TLS DllMain attach","TLS DllMain detach","TLS callback detach"})) return 1;
    events.clear();
    {
        wxl::Image image;
        if (!wxl::load(image,tls,log_line,carrier)) return 1;
        auto base=image.base; auto key=image.tlsKey; injected=READ_TLS;
        if (image.close() || image.base!=base || image.tlsKey!=key || !image.tlsAttached) return 1;
        if (!image.close() || image.base || image.tlsKey!=TLS_OUT_OF_INDEXES) return 1;
    }
    if (!sequence({"TLS callback attach","TLS DllMain attach","TLS DllMain detach","TLS callback detach"}) || count("WXL FAIL TLS read")!=1) return 1;
    events.clear();
    {
        wxl::Image image;
        if (wxl::load(image,tls_bad,log_line,carrier) || image.base || !image.close()) return 1;
    }
    if (!sequence({"TLS callback attach","TLS DllMain attach","TLS callback detach"})) return 1;
    puts("LIFECYCLE-OK"); return 0;
}
