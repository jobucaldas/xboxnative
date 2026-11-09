#include "route.h"
#include <cstdio>
#include <cstdlib>
#include <array>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
template<class F> F api(const char* name,F) {
    auto p=kernel32_core::resolve("kernel32.dll",name); CHECK(p);
    CHECK(p==kernel32_core::resolve("kernelbase.dll",name)); return reinterpret_cast<F>(p);
}
#define API(n) api(#n,&::n)
static LONG raised,broken;
static ULONG_PTR observed[2];
static LONG CALLBACK handler(EXCEPTION_POINTERS* p) {
    if(p->ExceptionRecord->ExceptionCode==0xe0421234) {
        raised++;
        if(p->ExceptionRecord->NumberParameters==2) {
            observed[0]=p->ExceptionRecord->ExceptionInformation[0];
            observed[1]=p->ExceptionRecord->ExceptionInformation[1];
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if(p->ExceptionRecord->ExceptionCode==EXCEPTION_BREAKPOINT) {
        broken++;
        if(*reinterpret_cast<const BYTE*>(p->ContextRecord->Rip)==0xcc) ++p->ContextRecord->Rip;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static unsigned filtered;
static LONG WINAPI filter(EXCEPTION_POINTERS* pointers) {
    if(pointers->ExceptionRecord->ExceptionCode==0xe0421234) {
        ++filtered; return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
int main() {
    PVOID registration=AddVectoredExceptionHandler(1,handler); CHECK(registration);
    ULONG_PTR args[2]={0x123456789abcdef0ULL,47};
    SetLastError(4321); API(RaiseException)(0xe0421234,0,2,args);
    CHECK(raised==1); CHECK(observed[0]==args[0]); CHECK(observed[1]==args[1]); CHECK(GetLastError()==4321);
    API(DebugBreak)(); CHECK(broken==1); CHECK(RemoveVectoredExceptionHandler(registration));
    auto setfilter=API(SetUnhandledExceptionFilter);
    auto previous=setfilter(filter); CHECK(setfilter(previous)==filter);
    previous=setfilter(filter);
    EXCEPTION_RECORD record{}; record.ExceptionCode=0xe0421234;
    CONTEXT fault_context{}; EXCEPTION_POINTERS fault{&record,&fault_context};
    CHECK(API(UnhandledExceptionFilter)(&fault)==EXCEPTION_CONTINUE_EXECUTION);
    CHECK(filtered==1); CHECK(setfilter(previous)==filter);
    API(OutputDebugStringW)(L"k32x off-device debug output regression\n");
    CONTEXT context{}; API(RtlCaptureContext)(&context);
    CHECK(context.Rip!=0); CHECK(context.Rsp!=0); CHECK(context.ContextFlags&CONTEXT_CONTROL);
    void* frames[32]{}; ULONG hash=0;
    USHORT depth=API(RtlCaptureStackBackTrace)(0,32,frames,&hash);
    CHECK(depth>0); CHECK(depth<=32); for(unsigned i=0;i<depth;++i)CHECK(frames[i]);
    CHECK(API(RtlCaptureStackBackTrace)(0,0,frames,&hash)==0);
    // Hand-built x64 metadata: push rbx; sub rsp,32; nop; add rsp,32;
    // pop rbx; ret. The dispatcher must restore the saved nonvolatile value
    // and pop the return PC using the native UNWIND_INFO encoding.
    alignas(16) BYTE image[32]={0x53,0x48,0x83,0xec,0x20,0x90,0x48,0x83,0xc4,0x20,0x5b,0xc3};
    image[16]=1; image[17]=5; image[18]=2;
    image[20]=5; image[21]=0x32; image[22]=1; image[23]=0x30;
    RUNTIME_FUNCTION function{0,12,16};
    alignas(16) DWORD64 stack[8]{}; stack[4]=0x1122334455667788ULL; stack[5]=0x12345678;
    context={}; context.Rsp=reinterpret_cast<DWORD64>(stack); context.Rip=reinterpret_cast<DWORD64>(image+5); context.Rbx=11;
    PVOID handler_data=nullptr; DWORD64 establisher=0;
    CHECK(!API(RtlVirtualUnwind)(UNW_FLAG_NHANDLER,reinterpret_cast<DWORD64>(image),context.Rip,&function,&context,&handler_data,&establisher,nullptr));
    CHECK(context.Rip==stack[5]); CHECK(context.Rbx==stack[4]); CHECK(context.Rsp==reinterpret_cast<DWORD64>(stack+6));
    CHECK(establisher==reinterpret_cast<DWORD64>(stack));
    CHECK(kernel32_core::resolve("ntdll.dll","RtlVirtualUnwind")==kernel32_core::resolve("kernel32.dll","RtlVirtualUnwind"));
    CHECK(!kernel32_core::resolve("ntdll.dll","DebugBreak"));
    // Growable function tables (Mono's JIT registers generated code this way).
    // The route must hand out the OS export: the native unwinder consults only
    // its own registry, so a private list would accept every table and then
    // never be read by RtlLookupFunctionEntry or RtlVirtualUnwind.
    {
        HMODULE ntdll=GetModuleHandleW(L"ntdll.dll"); CHECK(ntdll);
        auto raw_add=reinterpret_cast<decltype(&::RtlAddGrowableFunctionTable)>(
            reinterpret_cast<uintptr_t>(GetProcAddress(ntdll,"RtlAddGrowableFunctionTable")));
        auto raw_grow=reinterpret_cast<decltype(&::RtlGrowFunctionTable)>(
            reinterpret_cast<uintptr_t>(GetProcAddress(ntdll,"RtlGrowFunctionTable")));
        auto raw_drop=reinterpret_cast<decltype(&::RtlDeleteGrowableFunctionTable)>(
            reinterpret_cast<uintptr_t>(GetProcAddress(ntdll,"RtlDeleteGrowableFunctionTable")));
        auto lookup=reinterpret_cast<decltype(&::RtlLookupFunctionEntry)>(
            kernel32_core::resolve("ntdll.dll","RtlLookupFunctionEntry"));
        CHECK(lookup);
        auto add=reinterpret_cast<decltype(&::RtlAddGrowableFunctionTable)>(
            kernel32_core::resolve("ntdll.dll","RtlAddGrowableFunctionTable"));
        auto grow=reinterpret_cast<decltype(&::RtlGrowFunctionTable)>(
            kernel32_core::resolve("ntdll.dll","RtlGrowFunctionTable"));
        auto drop=reinterpret_cast<decltype(&::RtlDeleteGrowableFunctionTable)>(
            kernel32_core::resolve("ntdll.dll","RtlDeleteGrowableFunctionTable"));
        CHECK(add&&grow&&drop);
        // The refusal path is what a console without the export gets. It is
        // always exercised here: a table it never issued must not be reported
        // as registered, and the grow/delete handles stay inert.
        {
            void* refused=reinterpret_cast<void*>(0x1);
            CHECK(kernel32_core::add_growable_unsupported(&refused,nullptr,0,0,0,0)==kernel32_core::status_not_supported);
            CHECK(refused==reinterpret_cast<void*>(0x1));
            kernel32_core::grow_unsupported(nullptr,1);
            kernel32_core::delete_growable_unsupported(nullptr);
            CHECK(kernel32_core::status_not_supported==DWORD(0xC00000BB));
        }
        // Wine 10 exports all three from ntdll only; when the OS has them the
        // route is that exact function, never a local reimplementation.
        if(raw_add) {
            CHECK(add==raw_add); CHECK(grow==raw_grow); CHECK(drop==raw_drop);
            // The rtlsupport API set names the same ntdll runtime.
            CHECK(kernel32_core::resolve("api-ms-win-core-rtlsupport-l1-1-0.dll","RtlAddGrowableFunctionTable")==
                  reinterpret_cast<uintptr_t>(raw_add));
            // kernel32/kernelbase do not export the growable family.
            CHECK(!kernel32_core::resolve("kernel32.dll","RtlAddGrowableFunctionTable"));
            alignas(16) BYTE code[64]{};
            RUNTIME_FUNCTION functions[2]{};
            functions[0].BeginAddress=0; functions[0].EndAddress=16; functions[0].UnwindData=0;
            functions[1].BeginAddress=16; functions[1].EndAddress=32; functions[1].UnwindData=0;
            ULONG_PTR start=reinterpret_cast<ULONG_PTR>(code);
            void* table=nullptr;
            CHECK(add(&table,functions,1,2,start,start+64)==0); CHECK(table);
            // The registered entry resolves through the same unwinder, at the
            // registered range base.
            ULONG_PTR base=0;
            CHECK(lookup(start+8,&base,nullptr)==&functions[0]); CHECK(base==start);
            // Beyond the current count the second entry stays invisible.
            base=0xdead;
            CHECK(!lookup(start+24,&base,nullptr));
            grow(table,2);
            base=0;
            CHECK(lookup(start+24,&base,nullptr)==&functions[1]); CHECK(base==start);
            drop(table);
            // Deleted: the same query now matches the export's own answer.
            auto raw_lookup=reinterpret_cast<decltype(&::RtlLookupFunctionEntry)>(
                reinterpret_cast<uintptr_t>(GetProcAddress(ntdll,"RtlLookupFunctionEntry")));
            CHECK(raw_lookup);
            ULONG_PTR mine_base=0xdead,wine_base=0xdead;
            auto mine=lookup(start+8,&mine_base,nullptr);
            auto wine=raw_lookup(start+8,&wine_base,nullptr);
            CHECK(!mine&&!wine); CHECK(mine_base==wine_base);
        } else {
            // No OS export: the route is the documented refusal, never a local
            // table the unwinder would ignore.
            CHECK(add==reinterpret_cast<decltype(&::RtlAddGrowableFunctionTable)>(&kernel32_core::add_growable_unsupported));
            CHECK(grow==reinterpret_cast<decltype(&::RtlGrowFunctionTable)>(&kernel32_core::grow_unsupported));
            CHECK(drop==reinterpret_cast<decltype(&::RtlDeleteGrowableFunctionTable)>(&kernel32_core::delete_growable_unsupported));
        }
    }
    // Formatting compares native results, last errors and complete output
    // buffers. Insertion arguments use the documented argument-array form.
    ULONG_PTR inserts[]={reinterpret_cast<ULONG_PTR>(L"item"),42};
    for(DWORD capacity : {DWORD(0),DWORD(2),DWORD(128)}) {
        for(DWORD flags : {DWORD(FORMAT_MESSAGE_FROM_STRING|FORMAT_MESSAGE_IGNORE_INSERTS),
                           DWORD(FORMAT_MESSAGE_FROM_STRING|FORMAT_MESSAGE_ARGUMENT_ARRAY)}) {
            std::array<wchar_t,128> left,right; left.fill(0x5555); right.fill(0x5555);
            auto args_ptr=reinterpret_cast<va_list*>(inserts);
            SetLastError(123); DWORD n=FormatMessageW(flags,L"Value %1!s! = %2!u!.\r\n",0,0,left.data(),capacity,args_ptr); DWORD error=GetLastError();
            SetLastError(123); CHECK(API(FormatMessageW)(flags,L"Value %1!s! = %2!u!.\r\n",0,0,right.data(),capacity,args_ptr)==n);
            CHECK(GetLastError()==error); CHECK(left==right);
        }
    }
    auto formatw=API(FormatMessageW); LPWSTR message=nullptr;
    CHECK(formatw(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,ERROR_FILE_NOT_FOUND,0,reinterpret_cast<LPWSTR>(&message),0,nullptr));
    CHECK(message && *message); CHECK(!LocalFree(message));
    auto formata=API(FormatMessageA); char left[256]{},right[256]{};
    DWORD n=FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,ERROR_ACCESS_DENIED,0,left,sizeof(left),nullptr);
    CHECK(n>0); CHECK(formata(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,ERROR_ACCESS_DENIED,0,right,sizeof(right),nullptr)==n); CHECK(!strcmp(left,right));
    SetLastError(0); CHECK(!formata(FORMAT_MESSAGE_FROM_SYSTEM,nullptr,0xdeadbeef,0,right,sizeof(right),nullptr)); CHECK(GetLastError()==ERROR_MR_MID_NOT_FOUND);
    std::printf("kernel32 exception/messages: PASS (%u checks; off-device native dispatcher ABI)\n",checks);
}
