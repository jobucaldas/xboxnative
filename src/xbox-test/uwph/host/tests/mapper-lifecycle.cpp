// Off-device only: desktop substitutions validate host logic, not Xbox APIs.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
enum Failure { NONE, UNWIND, FREE_MEM, FREE_TLS, CLEAR_TLS, READ_TLS, SEEK, TELL };
static Failure injected=NONE;
static BOOL WINAPI test_delete(PRUNTIME_FUNCTION p) { if (injected==UNWIND) { injected=NONE; return FALSE; } return RtlDeleteFunctionTable(p); }
static BOOL WINAPI test_free(LPVOID p,SIZE_T n,DWORD t) { if (injected==FREE_MEM) { injected=NONE; return FALSE; } return VirtualFree(p,n,t); }
static BOOL WINAPI test_tls_free(DWORD k) { if (injected==FREE_TLS) { injected=NONE; return FALSE; } return TlsFree(k); }
static BOOL WINAPI test_tls_set(DWORD k,LPVOID v) { if (!v && injected==CLEAR_TLS) { injected=NONE; return FALSE; } return TlsSetValue(k,v); }
static LPVOID WINAPI test_tls_get(DWORD k) { if (injected==READ_TLS) { injected=NONE; SetLastError(ERROR_INVALID_PARAMETER); return nullptr; } return TlsGetValue(k); }
static int openFiles=0;
static FILE* test_wfopen(const wchar_t* p,const wchar_t* m) { FILE* f=_wfopen(p,m); if (f) ++openFiles; return f; }
static int test_fclose(FILE* f) { --openFiles; return fclose(f); }
static int test_fseek(FILE* f,long o,int w) { if (injected==SEEK) { injected=NONE; return -1; } return fseek(f,o,w); }
static long test_ftell(FILE* f) { if (injected==TELL) { injected=NONE; return -1L; } return ftell(f); }
#define _wfopen test_wfopen
#define fclose test_fclose
#define fseek test_fseek
#define ftell test_ftell
#define TlsGetValue test_tls_get
#define RtlDeleteFunctionTable test_delete
#define VirtualFree test_free
#define TlsFree test_tls_free
#define TlsSetValue test_tls_set
#define VirtualAllocFromApp VirtualAlloc
#define VirtualProtectFromApp VirtualProtect
#include "../wxl_mapper.h"
#undef _wfopen
#undef fclose
#undef fseek
#undef ftell
static std::vector<std::string> events;
static Failure attach_cleanup=NONE;
static DWORD expectedDetachKey=TLS_OUT_OF_INDEXES;
static bool missingDetachData=false;
static void log_line(const char* s) {
    puts(s); events.emplace_back(s);
    if (expectedDetachKey!=TLS_OUT_OF_INDEXES &&
        (!strcmp(s,"TLS DllMain detach") || !strcmp(s,"TLS DllMain thread detach")) &&
        !TlsGetValue(expectedDetachKey)) missingDetachData=true;
    if (!strcmp(s,"WXL FAIL DllMain process attach") && attach_cleanup!=NONE) {
        injected=attach_cleanup; attach_cleanup=NONE;
    }
}
static std::vector<std::string> guestLogsA,guestLogsB;
static HANDLE notificationEntered=nullptr,notificationContinue=nullptr;
static void log_a(const char* s) {
    if (!strncmp(s,"TLS ",4)) guestLogsA.emplace_back(s);
    if (notificationEntered && !strcmp(s,"TLS callback thread attach")) {
        SetEvent(notificationEntered);
        WaitForSingleObject(notificationContinue,INFINITE);
    }
}
static void log_b(const char* s) {
    if (!strncmp(s,"TLS ",4)) guestLogsB.emplace_back(s);
}
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
        if (count("LIFECYCLE attach")!=1 || count("LIFECYCLE detach")!=1 ||
            find("WXL FAIL DllMain process attach")<0 ||
            find("WXL RtlDeleteFunctionTable ok=1")<find("WXL FAIL DllMain process attach")) return 1;
        if (!image.close()) return 1;
    }
    if (count("LIFECYCLE detach")!=1 || count("WXL RtlDeleteFunctionTable ok=1")!=1) return 1;
    // A failed attach can also fail cleanup. Keep ownership until a retry,
    // reject replacement while live, then allow reuse after cleanup succeeds.
    for (Failure f : {UNWIND, FREE_MEM}) {
        events.clear(); wxl::Image image; attach_cleanup=f;
        if (wxl::load(image,bad,log_line) || injected!=NONE || !image.base || image.attached) return 1;
        if ((f==UNWIND)!=!!image.functions || count("WXL FAIL cleanup incomplete")!=1) return 1;
        auto base=image.base;
        if (wxl::load(image,good,log_line) || image.base!=base || count("LIFECYCLE attach")!=1 ||
            count("WXL FAIL image already live")!=1) return 1;
        if (!image.close() || !image.close() || image.base || image.functions || count("LIFECYCLE detach")!=1) return 1;
        if (!wxl::load(image,good,log_line) || !image.close() || count("LIFECYCLE detach")!=2) return 1;
    }
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
    if (!sequence({"TLS callback attach","TLS DllMain attach","TLS callback detach","TLS DllMain detach"})) return 1;
    events.clear();
    {
        wxl::Image image;
        if (!wxl::load(image,tls,log_line,carrier)) return 1;
        injected=CLEAR_TLS;
        if (image.close() || !image.close()) return 1;
    }
    if (!sequence({"TLS callback attach","TLS DllMain attach","TLS callback detach","TLS DllMain detach"})) return 1;
    events.clear();
    {
        wxl::Image image;
        if (!wxl::load(image,tls,log_line,carrier)) return 1;
        auto base=image.base; auto key=image.tlsKey; injected=READ_TLS;
        if (image.close() || image.base!=base || image.tlsKey!=key || !image.tlsAttached) return 1;
        if (!image.close() || image.base || image.tlsKey!=TLS_OUT_OF_INDEXES) return 1;
    }
    if (!sequence({"TLS callback attach","TLS DllMain attach","TLS callback detach","TLS DllMain detach"}) || count("WXL FAIL TLS read")!=1) return 1;
    events.clear();
    {
        wxl::Image image;
        if (wxl::load(image,tls_bad,log_line,carrier) || image.base || !image.close()) return 1;
    }
    if (!sequence({"TLS callback attach","TLS DllMain attach","TLS callback detach","TLS DllMain detach"})) return 1;
    for (Failure f : {READ_TLS, CLEAR_TLS, FREE_TLS}) {
        events.clear(); wxl::Image image; attach_cleanup=f;
        if (wxl::load(image,tls_bad,log_line,carrier) || injected!=NONE || !image.base ||
            image.attached!=(f==READ_TLS) || image.tlsKey==TLS_OUT_OF_INDEXES || count("WXL FAIL cleanup incomplete")!=1) return 1;
        auto base=image.base; auto key=image.tlsKey;
        if (wxl::load(image,tls,log_line,carrier) || image.base!=base || image.tlsKey!=key ||
            count("TLS callback attach")!=1 || count("WXL FAIL image already live")!=1) return 1;
        if (!image.close() || !image.close() || image.base || image.functions ||
            image.tlsKey!=TLS_OUT_OF_INDEXES || image.tlsAttached) return 1;
        if (!sequence({"TLS callback attach","TLS DllMain attach","TLS callback detach","TLS DllMain detach"})) return 1;
        events.clear();
        if (!wxl::load(image,tls,log_line,carrier) || !image.close()) return 1;
        if (!sequence({"TLS callback attach","TLS DllMain attach","TLS callback detach","TLS DllMain detach"})) return 1;
    }
    events.clear();
    {
        wxl::Image image;
        if (!wxl::load(image,tls,log_line,carrier)) return 1;
        expectedDetachKey=image.tlsKey;
        HANDLE thread=CreateThread(nullptr,0,[](void* p)->DWORD {
            auto& guest=*(wxl::Image*)p;
            if (!guest.attachThread(DLL_THREAD_ATTACH)) return 1;
            log_line("LIFECYCLE worker ran after FALSE thread attach");
            if (!guest.detachThread(DLL_THREAD_DETACH)) return 1;
            return 0;
        },&image,0,nullptr);
        if (!thread) return 1;
        DWORD status=1;
        bool joined=WaitForSingleObject(thread,INFINITE)==WAIT_OBJECT_0 && GetExitCodeThread(thread,&status);
        CloseHandle(thread);
        if (!joined || status!=0 || !image.close() || missingDetachData) return 1;
        expectedDetachKey=TLS_OUT_OF_INDEXES;
    }
    if (count("LIFECYCLE worker ran after FALSE thread attach")!=1 ||
        !sequence({"TLS callback attach","TLS DllMain attach",
                   "TLS callback thread attach","TLS DllMain thread attach",
                   "TLS callback thread detach","TLS DllMain thread detach",
                   "TLS callback detach","TLS DllMain detach"})) return 1;
    // Failed thread-detach TLS reads/clears are reported, never ignored.
    // A failed read runs no thread notification: the slot state is unknown.
    for (Failure f : {READ_TLS, CLEAR_TLS}) {
        events.clear();
        wxl::Image image;
        if (!wxl::load(image,tls,log_line,carrier)) return 1;
        static Failure detachFailure; detachFailure=f;
        HANDLE thread=CreateThread(nullptr,0,[](void* p)->DWORD {
            auto& guest=*(wxl::Image*)p;
            if (!guest.attachThread(DLL_THREAD_ATTACH)) return 1;
            injected=detachFailure;
            return guest.detachThread(DLL_THREAD_DETACH) ? 0 : 2;
        },&image,0,nullptr);
        if (!thread) return 1;
        DWORD status=1;
        bool joined=WaitForSingleObject(thread,INFINITE)==WAIT_OBJECT_0 && GetExitCodeThread(thread,&status);
        CloseHandle(thread);
        if (!joined || status!=2 || injected!=NONE || !image.close()) return 1;
        bool read=f==READ_TLS;
        if (count(read?"WXL FAIL thread TLS read":"WXL FAIL thread TLS clear")!=1) return 1;
        if (read ? !sequence({"TLS callback attach","TLS DllMain attach",
                              "TLS callback thread attach","TLS DllMain thread attach",
                              "TLS callback detach","TLS DllMain detach"})
                 : !sequence({"TLS callback attach","TLS DllMain attach",
                              "TLS callback thread attach","TLS DllMain thread attach",
                              "TLS callback thread detach","TLS DllMain thread detach",
                              "TLS callback detach","TLS DllMain detach"})) return 1;
    }
    // Loading B while A is executing must not steal A's callback logger.
    // Close A first as well: it must not inherit the last-loaded B's sink.
    {
        wxl::Image a,b,missing;
        if (!wxl::load(a,tls,log_a,carrier)) return 1;
        notificationEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        notificationContinue=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if (!notificationEntered || !notificationContinue) return 1;
        HANDLE thread=CreateThread(nullptr,0,[](void* p)->DWORD {
            auto& image=*(wxl::Image*)p;
            return image.attachThread(DLL_THREAD_ATTACH) && image.detachThread(DLL_THREAD_DETACH) ? 0 : 1;
        },&a,0,nullptr);
        if (!thread) return 1;
        if (WaitForSingleObject(notificationEntered,INFINITE)!=WAIT_OBJECT_0) return 1;
        bool loaded=wxl::load(b,tls,log_b,carrier);
        bool rejected=!wxl::load(missing,L"out-tls/tests/missing-image.dll",log_b);
        SetEvent(notificationContinue);
        DWORD status=1;
        bool joined=WaitForSingleObject(thread,INFINITE)==WAIT_OBJECT_0 && GetExitCodeThread(thread,&status);
        CloseHandle(thread); CloseHandle(notificationEntered); CloseHandle(notificationContinue);
        notificationEntered=notificationContinue=nullptr;
        if (!loaded || !rejected || !joined || status || !a.close() || !b.close()) return 1;
        const std::vector<std::string> wantA={"TLS callback attach","TLS DllMain attach",
            "TLS callback thread attach","TLS DllMain thread attach",
            "TLS callback thread detach","TLS DllMain thread detach",
            "TLS callback detach","TLS DllMain detach"};
        const std::vector<std::string> wantB={"TLS callback attach","TLS DllMain attach",
            "TLS callback detach","TLS DllMain detach"};
        if (guestLogsA!=wantA || guestLogsB!=wantB) return 1;
    }
    puts("LIFECYCLE concurrent per-image guest log routing ok");
    // A failed size query (-1) must be rejected without allocating, throwing
    // or leaking the file.
    for (Failure f : {SEEK, TELL}) {
        events.clear(); wxl::Image image; injected=f;
        bool loaded=true;
        try { loaded=wxl::load(image,good,log_line); } catch (...) { return 1; }
        if (loaded || injected!=NONE || openFiles || image.base ||
            count("WXL FAIL file size query")!=1 || count("LIFECYCLE attach")) return 1;
        if (!wxl::load(image,good,log_line) || !image.close() || openFiles) return 1;
    }
    puts("LIFECYCLE thread-detach TLS failures and file size query failures rejected");
    puts("LIFECYCLE native notification ordering and ignored thread result ok");
    puts("LIFECYCLE failed-load cleanup retry/reuse ok");
    puts("LIFECYCLE-OK"); return 0;
}
