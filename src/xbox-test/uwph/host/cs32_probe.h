#pragma once
// Isolated feasibility probe. No descriptor installation, TEB writes or private syscalls.
namespace cs32 {
static LONG fault(EXCEPTION_POINTERS* e) {
    auto c=e->ContextRecord;
    logf("CS32 EXCEPTION code=0x%08lx address=%p RIP=0x%llx CS=%04x SS=%04x DS=%04x ES=%04x FS=%04x GS=%04x",
         e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress,c->Rip,
         c->SegCs,c->SegSs,c->SegDs,c->SegEs,c->SegFs,c->SegGs);
    if(e->ExceptionRecord->NumberParameters>=2)
        logf("CS32 fault access=%llu target=0x%llx",e->ExceptionRecord->ExceptionInformation[0],e->ExceptionRecord->ExceptionInformation[1]);
    return EXCEPTION_EXECUTE_HANDLER;
}
static bool execute(void* p, unsigned* result) {
    __try { *result=((unsigned(*)())p)(); return true; }
    __except(fault(GetExceptionInformation())) { return false; }
}
static void run() {
    CONTEXT c={}; RtlCaptureContext(&c);
    logf("CS32 selectors CS=%04x SS=%04x DS=%04x ES=%04x FS=%04x GS=%04x",
         c.SegCs,c.SegSs,c.SegDs,c.SegEs,c.SegFs,c.SegGs);
    // Only packaged loading is permitted; absence does not prove OS-wide absence.
    const wchar_t* names[]={L"wow64.dll",L"wow64cpu.dll",L"wow64win.dll"};
    for(auto n:names) {
        HMODULE m=LoadPackagedLibrary(n,0); DWORD err=m?0:GetLastError();
        logf("CS32 packaged WoW64 %ls module=%p error=%lu",n,m,err);
        if(m) FreeLibrary(m);
    }
    logf("CS32 WoW64 directory/process desktop probes omitted: no documented UWP contract; no private WowTebOffset access");
    unsigned char* code=nullptr;
    unsigned attempts=0; DWORD err=0;
    for(uintptr_t addr=0x10000000;addr<0x70000000;addr+=0x1000000) {
        ++attempts;
        code=(unsigned char*)VirtualAllocFromApp((void*)addr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(code) break;
        err=GetLastError();
    }
    logf("CS32 low alloc=%p attempts=%u error=%lu",code,attempts,code?0:err);
    if(!code) { logf("CS32 RESULT native-32=no low allocation denied"); return; }
    // x64: jmp far m16:64 [rip+9]; x86: mov eax,42; jmp far 33:low_return.
    // No 32-bit stack use. The original x64 RSP remains intact for ret in long mode.
    unsigned char bytes[]={0x48,0xff,0x2d,9,0,0,0,0xb8,42,0,0,0,0xea,0,0,0,0,0x33,0,0,0,0,0,0,0,0,0,0,0xc3};
    memcpy(code,bytes,sizeof bytes);
    // Keep the far pointer separate from the instruction stream at offset 64.
    *(int*)(code+3)=64-7;
    *(unsigned*)(code+13)=(unsigned)(uintptr_t)(code+28);
    *(uint64_t*)(code+64)=(uintptr_t)(code+7);
    *(unsigned short*)(code+72)=0x23;
    const unsigned char control[]={0xb8,42,0,0,0,0xc3};
    memcpy(code+96,control,sizeof control);
    DWORD old=0;
    BOOL ok=VirtualProtectFromApp(code,4096,PAGE_EXECUTE_READ,&old);
    logf("CS32 W^X RX=%d old=0x%lx error=%lu targetCS=0023 returnCS=0033",ok,old,ok?0:GetLastError());
    if(!ok) { VirtualFree(code,0,MEM_RELEASE); logf("CS32 RESULT native-32=no RX denied"); return; }
    FlushInstructionCache(GetCurrentProcess(),code,4096);
    unsigned result=0;
    bool control_ok=execute(code+96,&result);
    logf("CS32 x64 control survived=%d result=%u",control_ok,result);
    if(!control_ok || result!=42) { VirtualFree(code,0,MEM_RELEASE); logf("CS32 RESULT inconclusive x64 control failed"); return; }
    result=0;
    logf("CS32 entering ljmp 0x23; FS selector=%04x; no FS descriptor mutation",c.SegFs);
    bool survived=execute(code,&result);
    logf("CS32 roundtrip survived=%d result=%u",survived,result);
    if(survived && result==42) {
        logf("CS32-OK minimal native roundtrip only");
        // Read fs:[0x18] without installing a selector; separate failure from CS transition.
        ok=VirtualProtectFromApp(code,4096,PAGE_READWRITE,&old);
        if(ok) {
            const unsigned char fsread[]={0x64,0xa1,0x18,0,0,0,0xea,0,0,0,0,0x33,0};
            memcpy(code+7,fsread,sizeof fsread);
            *(unsigned*)(code+14)=(unsigned)(uintptr_t)(code+28);
            ok=VirtualProtectFromApp(code,4096,PAGE_EXECUTE_READ,&old);
            logf("CS32 FS strategy RX=%d error=%lu",ok,ok?0:GetLastError());
            if(ok) {
                FlushInstructionCache(GetCurrentProcess(),code,4096);
                survived=execute(code,&result);
                logf("CS32 FS:[18] survived=%d value=0x%08x; TEB32 identity not established",survived,result);
            }
        }
    } else logf("CS32 RESULT native-32=no selector transition denied");
    VirtualFree(code,0,MEM_RELEASE);
    logf("CS32 DONE");
}
}
