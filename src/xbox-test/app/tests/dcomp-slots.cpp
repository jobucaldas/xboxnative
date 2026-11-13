// SPDX-License-Identifier: GPL-3.0-or-later
// DComp vtable-slot audit (bugbot r4559524004). The dead page in main.cpp
// drives IDCompositionDevice/Target/Visual; before this test existed the
// slots were hand-counted and SetContent landed on slot 21 when the real
// dcomp.h puts it at slot 15 — a call through the hand-declared interface
// forwarded into the middle of the real vtable. Hand-counted slots are a
// recurring bug class in this repository, so every slot main.cpp relies on
// is derived here from the REAL header (the same mingw-w64 dcomp.h CI's
// toolchain ships) and asserted.
//
// Derivation: the Itanium C++ ABI encodes a virtual method's vtable index in
// the pointer-to-member-function's ptr field as the vtable BYTE offset
// (slot * 8 on x64); a memcpy through unsigned char storage avoids any
// reinterpret-cast of a PMF. The mingw g++ that compiles this file and the
// clang-cl crossbuild that compiles main.cpp both target the Windows x64
// ABI, where the MSVC layout these numbers describe is the only layout.
#include <windows.h>
// dcomp.h is Desktop-partitioned; main.cpp includes it with the same shim.
#pragma push_macro("WINAPI_FAMILY")
#undef WINAPI_FAMILY
#define WINAPI_FAMILY WINAPI_FAMILY_DESKTOP_APP
#include <dcomp.h>
#pragma pop_macro("WINAPI_FAMILY")
#include <cstdio>
#include <cstring>

// Slot of a virtual method, from the real header's declaration.
template <typename Iface>
struct Slot {
    template <typename M>
    static long of(M pmf) {
        unsigned char buf[sizeof(M)];
        std::memcpy(buf, &pmf, sizeof(M));
        void* p; std::memcpy(&p, buf, sizeof(p));
        if (!p || p == (void*)1) return -1;   // virtual-inheritance thunk: not a plain vtable offset
        return (long)((uintptr_t)p) / 8;      // Itanium x64: ptr = vtable byte offset; /8 = slot
    }
};

static unsigned checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL line %d: %s\n",__LINE__,#x);} } while(0)

int main() {
    // IDCompositionVisual (main.cpp calls SetContent): IUnknown's 3, then
    // SetOffsetX x2, SetOffsetY x2, SetTransform x2, SetTransformParent,
    // SetEffect, SetBitmapInterpolationMode, SetBorderMode, SetClip x2 —
    // SetContent is the 13th method, slot 15. The hand-count that said 21
    // (18 placeholders) was wrong; this is the assertion that pins the real
    // header's order.
    const long setContent = Slot<IDCompositionVisual>::of(
        static_cast<HRESULT (STDMETHODCALLTYPE IDCompositionVisual::*)(IUnknown*)>(&IDCompositionVisual::SetContent));
    CHECK(setContent == 15);

    // IDCompositionDevice (main.cpp calls Commit, CreateTargetForHwnd,
    // CreateVisual).
    CHECK(Slot<IDCompositionDevice>::of(&IDCompositionDevice::Commit) == 3);
    CHECK(Slot<IDCompositionDevice>::of(&IDCompositionDevice::CreateTargetForHwnd) == 6);
    CHECK(Slot<IDCompositionDevice>::of(&IDCompositionDevice::CreateVisual) == 7);

    // IDCompositionTarget (main.cpp calls SetRoot).
    CHECK(Slot<IDCompositionTarget>::of(&IDCompositionTarget::SetRoot) == 3);

    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
