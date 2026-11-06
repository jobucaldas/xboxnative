// SPDX-License-Identifier: GPL-3.0-or-later
// Owned /MD C++ DLL: imports vcruntime140.dll (and vcruntime140_1.dll through
// play-vcrt1.lib) the way a game built against the VC++ redistributable does.
// Play must bind every such import to ucrtbase (or an owned vcruntime helper)
// without the redistributable, and C++ exceptions/RTTI must work through it.
#include <windows.h>
#include <exception>
#include <typeinfo>
#include <string.h>
struct Base {virtual ~Base() {}};
struct Derived : Base {int value = 7;};
struct Other : Base {};
struct Error {int code;};
static int destructors, unwinding;
struct Guard {~Guard() {++destructors;}};
extern "C" int __cdecl __uncaught_exceptions(void);  // owned by the route, not ucrtbase
struct Observer {~Observer() {unwinding = __uncaught_exceptions();}};
__declspec(noinline) static void raise(int code) {Guard guard; Observer observer; throw Error{code};}
__declspec(noinline) static void rethrow() {try {raise(5);} catch (...) {throw;}}
extern "C" __declspec(dllimport) void __cdecl __CxxFrameHandler4(void);  // vcruntime140_1.dll, play-vcrt1.lib
extern "C" __declspec(dllexport) void* vcrt_frame_handler4 = (void*)&__CxxFrameHandler4;

static IMAGE_IMPORT_DESCRIPTOR* imports(HMODULE self, const char* dll) {
    auto base = (BYTE*)self;
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; ++d)
        if (!_stricmp((char*)base + d->Name, dll)) return d;
    return nullptr;
}
// Every import of `dll` must be bound: to ucrtbase's export of the same name,
// or (for the owned vcruntime helpers) to a non-ucrtbase address. Returns the
// number bound to ucrtbase, or -1.
static int bound(HMODULE self, HMODULE ucrt, const char* dll, int* owned) {
    auto d = imports(self, dll);
    if (!d) return -1;
    auto base = (BYTE*)self;
    auto names = (IMAGE_THUNK_DATA*)(base + d->OriginalFirstThunk);
    auto slots = (IMAGE_THUNK_DATA*)(base + d->FirstThunk);
    int ucrtbase = 0;
    for (; names->u1.AddressOfData; ++names, ++slots) {
        if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) return -1;
        auto name = (const char*)((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name;
        auto native = (ULONG_PTR)GetProcAddress(ucrt, name);
        if (!slots->u1.Function) return -1;
        if (slots->u1.Function == native) ++ucrtbase;
        else if (strcmp(name, "__uncaught_exceptions") && strcmp(name, "_CxxThrowException")) return -1;
        else ++*owned;
    }
    return ucrtbase;
}
extern "C" __declspec(dllexport) int native_vcrt_test() {
    // The redistributable is absent: no module of that name is ever loaded.
    if (GetModuleHandleW(L"vcruntime140.dll") || GetModuleHandleW(L"vcruntime140_1.dll")) return 10;
    HMODULE self = GetModuleHandleW(L"play-vcrt.dll"), ucrt = GetModuleHandleW(L"ucrtbase.dll");
    if (!self || !ucrt) return 11;
    int owned = 0;
    int routed = bound(self, ucrt, "vcruntime140.dll", &owned);
    if (routed < 5 || owned != 2) return 12;  // __uncaught_exceptions, _CxxThrowException are owned
    int routed1 = bound(self, ucrt, "vcruntime140_1.dll", &owned);
    if (routed1 != 1 || vcrt_frame_handler4 != (void*)GetProcAddress(ucrt, "__CxxFrameHandler4")) return 13;
    // FH3 throw/catch with unwinding destructors and the uncaught count.
    destructors = 0; unwinding = -1;
    try {raise(73);} catch (const Error& error) {if (error.code != 73) return 20;}
    if (destructors != 1 || unwinding != 1 || __uncaught_exceptions() != 0) return 21;
    // Rethrow of the current exception (ucrtbase per-thread exception state).
    try {rethrow();} catch (const Error& error) {if (error.code != 5 || destructors != 2) return 22;}
    catch (...) {return 23;}
    // RTTI: typeid names, dynamic_cast success, failure, and std::bad_cast.
    Derived derived; Other other; Base* object = &derived;
    if (!strstr(typeid(*object).name(), "Derived")) return 30;
    if (dynamic_cast<Derived*>(object)->value != 7 || dynamic_cast<Derived*>((Base*)&other)) return 31;
    try {(void)dynamic_cast<Derived&>(*(Base*)&other); return 32;}
    catch (const std::bad_cast& error) {if (!error.what() || !*error.what()) return 33;}
    catch (...) {return 34;}
    // std::exception copy/destroy (__std_exception_copy) across a rethrow.
    try {try {throw std::bad_alloc();} catch (const std::exception&) {throw;}}
    catch (const std::bad_alloc& error) {if (!strstr(error.what(), "alloc")) return 35;}
    return 42;
}
