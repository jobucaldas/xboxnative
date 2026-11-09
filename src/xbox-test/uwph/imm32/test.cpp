#include "route.h"
#include <cstdio>
#include <array>
// mingw's imm.h omits the lock family; the ABI is imm32.spec's.
extern "C" {
LPVOID WINAPI ImmLockIMC(HIMC);
BOOL WINAPI ImmUnlockIMC(HIMC);
LPVOID WINAPI ImmLockIMCC(HIMCC);
BOOL WINAPI ImmUnlockIMCC(HIMCC);
}
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    HMODULE reference = LoadLibraryW(L"imm32.dll");
    CHECK(reference);
    const char* names[] = {"ImmGetContext", "ImmGetCompositionStringW",
        "ImmGetConversionStatus", "ImmSetCompositionStringW", "ImmNotifyIME", "ImmReleaseContext"};
    CHECK(!imm32_core::resolve(nullptr, names[0]));
    CHECK(!imm32_core::resolve("imm32.dll", nullptr));
    CHECK(!imm32_core::resolve("user32.dll", names[0]));
    CHECK(imm32_core::resolve("imm32.dll", "ImmAssociateContext"));
    CHECK(!imm32_core::resolve("imm32.dll", "ImmCreateContext"));
    CHECK(!imm32_core::resolve("imm32.dll", "immgetcontext"));
    // Run the same typed ABI calls against our resolver and Wine's export.
    // Null and foreign contexts cover the supported backend state without assuming that
    // host Wine HWNDs or IME contexts belong to the guest.
    for (HIMC context : {HIMC(nullptr), reinterpret_cast<HIMC>(0x5678)})
    for (auto name : names) {
        auto candidate = imm32_core::resolve("IMM32.DLL", name);
        auto oracle = reinterpret_cast<uintptr_t>(GetProcAddress(reference, name));
        CHECK(candidate && oracle);
        LONG results[2]{};
        DWORD errors[2]{}, conversion[2]{0xabcdef, 0xabcdef}, sentence[2]{0x123456, 0x123456};
        std::array<unsigned char, 16> buffers[2];
        for (int i = 0; i < 2; ++i) {
            buffers[i].fill(0xa5);
            uintptr_t address = i ? oracle : candidate;
            WCHAR composition[] = L"x";
            SetLastError(0xdead);
            if (!strcmp(name, "ImmGetContext"))
                results[i] = reinterpret_cast<decltype(&ImmGetContext)>(address)(nullptr) != nullptr;
            else if (!strcmp(name, "ImmGetCompositionStringW"))
                results[i] = reinterpret_cast<decltype(&ImmGetCompositionStringW)>(address)(context, GCS_COMPSTR, buffers[i].data(), 16);
            else if (!strcmp(name, "ImmGetConversionStatus"))
                results[i] = reinterpret_cast<decltype(&ImmGetConversionStatus)>(address)(context, &conversion[i], &sentence[i]);
            else if (!strcmp(name, "ImmSetCompositionStringW"))
                results[i] = reinterpret_cast<decltype(&ImmSetCompositionStringW)>(address)(context, SCS_SETSTR, composition, 2, nullptr, 0);
            else if (!strcmp(name, "ImmNotifyIME"))
                results[i] = reinterpret_cast<decltype(&ImmNotifyIME)>(address)(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            else
                results[i] = reinterpret_cast<decltype(&ImmReleaseContext)>(address)(nullptr, context);
            errors[i] = GetLastError();
        }
        if (results[0] != results[1] || errors[0] != errors[1])
            std::printf("%s candidate=%ld/%lu Wine=%ld/%lu\n", name, results[0], errors[0], results[1], errors[1]);
        CHECK(results[0] == results[1] && errors[0] == errors[1]);
        CHECK(conversion[0] == 0xabcdef && sentence[0] == 0x123456);
        CHECK(conversion[0] == conversion[1] && sentence[0] == sentence[1]);
        for (auto value : buffers[0]) CHECK(value == 0xa5);
        CHECK(buffers[0] == buffers[1]);
    }
    // No context is ever manufactured for arbitrary guest handles. No buffer
    // pointer is touched when its context is absent.
    CHECK(!imm32_core::get_context(reinterpret_cast<HWND>(0x1234)));
    CHECK(!imm32_core::get_conversion(nullptr, nullptr, nullptr));
    CHECK(!imm32_core::get_composition(nullptr, GCS_RESULTSTR, nullptr, 0));
    CHECK(imm32_core::release_context(reinterpret_cast<HWND>(0x1234), nullptr));
    auto associate = reinterpret_cast<decltype(&ImmAssociateContext)>(
        imm32_core::resolve("imm32.dll", "ImmAssociateContext"));
    auto associate_ex = reinterpret_cast<decltype(&ImmAssociateContextEx)>(
        imm32_core::resolve("imm32.dll", "ImmAssociateContextEx"));
    auto oracle_associate = reinterpret_cast<decltype(&ImmAssociateContext)>(
        GetProcAddress(reference, "ImmAssociateContext"));
    auto oracle_ex = reinterpret_cast<decltype(&ImmAssociateContextEx)>(
        GetProcAddress(reference, "ImmAssociateContextEx"));
    CHECK(associate && associate_ex && oracle_associate && oracle_ex);
    SetLastError(0xdead);
    CHECK(!associate(nullptr, nullptr));
    DWORD error = GetLastError();
    SetLastError(0xdead);
    CHECK(!oracle_associate(nullptr, nullptr));
    CHECK(error == GetLastError());
    for (DWORD flags : {DWORD(0), DWORD(IACE_CHILDREN), DWORD(IACE_DEFAULT),
                        DWORD(IACE_IGNORENOCONTEXT), DWORD(0xffffffff)}) {
        SetLastError(0xdead);
        CHECK(!associate_ex(nullptr, nullptr, flags));
        error = GetLastError();
        SetLastError(0xdead);
        CHECK(!oracle_ex(nullptr, nullptr, flags));
        CHECK(error == GetLastError());
    }
    // A host regression supplies a guest-record validator, never host IsWindow.
    HWND record = reinterpret_cast<HWND>(0x1234);
    SetLastError(0xdead);
    CHECK(!associate_ex(record, nullptr, 0));
    CHECK(GetLastError() == ERROR_NOT_SUPPORTED);
    imm32_core::window_validator = +[](HWND h) WINAPI -> BOOL {
        return h == reinterpret_cast<HWND>(0x1234);
    };
    SetLastError(0xdead);
    CHECK(!imm32_core::get_context(record) && GetLastError() == 0xdead);
    CHECK(!imm32_core::get_context(reinterpret_cast<HWND>(0x9999)));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    for (DWORD flags : {DWORD(0), DWORD(IACE_CHILDREN), DWORD(IACE_IGNORENOCONTEXT)}) {
        SetLastError(0xdead);
        CHECK(associate_ex(record, nullptr, flags));
        CHECK(GetLastError() == 0xdead);
    }
    SetLastError(0xdead);
    CHECK(!associate(record, nullptr) && GetLastError() == 0xdead);
    CHECK(!associate_ex(record, nullptr, IACE_DEFAULT));
    CHECK(GetLastError() == ERROR_NOT_SUPPORTED);
    HIMC foreign = reinterpret_cast<HIMC>(0x5678);
    CHECK(!associate(record, foreign) && GetLastError() == ERROR_INVALID_HANDLE);
    CHECK(!associate_ex(record, foreign, 0) && GetLastError() == ERROR_INVALID_HANDLE);
    CHECK(!associate_ex(reinterpret_cast<HWND>(0x9999), nullptr, 0));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    SetLastError(0xdead);
    CHECK(!associate_ex(record, nullptr, IACE_CHILDREN | IACE_DEFAULT));
    CHECK(GetLastError() == 0xdead);
    // ANSI composition string: the same no-context result as Wine, buffer untouched.
    {
        auto ansi = reinterpret_cast<decltype(&ImmGetCompositionStringA)>(imm32_core::resolve("imm32.dll", "ImmGetCompositionStringA"));
        auto oracle = reinterpret_cast<decltype(&ImmGetCompositionStringA)>(GetProcAddress(reference, "ImmGetCompositionStringA"));
        CHECK(ansi && oracle);
        char mine[16], wine[16]; std::memset(mine, 0xa5, sizeof mine); std::memset(wine, 0xa5, sizeof wine);
        SetLastError(0xdead); LONG r1 = ansi(nullptr, GCS_COMPSTR, mine, sizeof mine); DWORD e1 = GetLastError();
        SetLastError(0xdead); LONG r2 = oracle(nullptr, GCS_COMPSTR, wine, sizeof wine); DWORD e2 = GetLastError();
        CHECK(r1 == r2 && e1 == e2 && !std::memcmp(mine, wine, sizeof mine));
    }
    {
        auto mine = reinterpret_cast<decltype(&ImmSimulateHotKey)>(imm32_core::resolve("imm32.dll", "ImmSimulateHotKey"));
        auto wine = reinterpret_cast<decltype(&ImmSimulateHotKey)>(GetProcAddress(reference, "ImmSimulateHotKey"));
        CHECK(mine && wine);
        SetLastError(0xdead); BOOL r1 = mine(nullptr, IME_CHOTKEY_IME_NONIME_TOGGLE); DWORD e1 = GetLastError();
        SetLastError(0xdead); BOOL r2 = wine(nullptr, IME_CHOTKEY_IME_NONIME_TOGGLE); DWORD e2 = GetLastError();
        if (r1 != r2 || e1 != e2) std::printf("ImmSimulateHotKey mine=%d/%lu Wine=%d/%lu\n", r1, e1, r2, e2);
        CHECK(r1 == r2 && e1 == e2);
    }
    // Context and component locks. No context exists here, so ImmLockIMC has
    // nothing to return; an IMCC block is plain global memory in Wine, so the
    // lock pair must agree with Wine on a real movable block as well as on
    // null and foreign handles.
    {
        auto mine_lock = reinterpret_cast<decltype(&ImmLockIMC)>(imm32_core::resolve("imm32.dll", "ImmLockIMC"));
        auto mine_unlock = reinterpret_cast<decltype(&ImmUnlockIMC)>(imm32_core::resolve("imm32.dll", "ImmUnlockIMC"));
        auto mine_lockcc = reinterpret_cast<decltype(&ImmLockIMCC)>(imm32_core::resolve("imm32.dll", "ImmLockIMCC"));
        auto mine_unlockcc = reinterpret_cast<decltype(&ImmUnlockIMCC)>(imm32_core::resolve("imm32.dll", "ImmUnlockIMCC"));
        auto mine_disable = reinterpret_cast<decltype(&ImmDisableTextFrameService)>(imm32_core::resolve("imm32.dll", "ImmDisableTextFrameService"));
        auto wine_lock = reinterpret_cast<decltype(&ImmLockIMC)>(GetProcAddress(reference, "ImmLockIMC"));
        auto wine_unlock = reinterpret_cast<decltype(&ImmUnlockIMC)>(GetProcAddress(reference, "ImmUnlockIMC"));
        auto wine_lockcc = reinterpret_cast<decltype(&ImmLockIMCC)>(GetProcAddress(reference, "ImmLockIMCC"));
        auto wine_unlockcc = reinterpret_cast<decltype(&ImmUnlockIMCC)>(GetProcAddress(reference, "ImmUnlockIMCC"));
        auto wine_disable = reinterpret_cast<decltype(&ImmDisableTextFrameService)>(GetProcAddress(reference, "ImmDisableTextFrameService"));
        CHECK(mine_lock && mine_unlock && mine_lockcc && mine_unlockcc && mine_disable);
        CHECK(wine_lock && wine_unlock && wine_lockcc && wine_unlockcc && wine_disable);
        // Null and foreign contexts: no backend context is ever manufactured.
        for (HIMC context : {HIMC(nullptr), reinterpret_cast<HIMC>(0x5678)}) {
            SetLastError(0xdead); void* mine = mine_lock(context); DWORD e1 = GetLastError();
            SetLastError(0xdead); void* wine = wine_lock(context); DWORD e2 = GetLastError();
            if (mine != wine || e1 != e2) std::printf("ImmLockIMC mine=%p/%lu Wine=%p/%lu\n", mine, e1, wine, e2);
            CHECK(mine == wine && e1 == e2);
            SetLastError(0xdead); BOOL ur = mine_unlock(context); e1 = GetLastError();
            SetLastError(0xdead); BOOL wr = wine_unlock(context); e2 = GetLastError();
            if (ur != wr || e1 != e2) std::printf("ImmUnlockIMC mine=%d/%lu Wine=%d/%lu\n", ur, e1, wr, e2);
            CHECK(ur == wr && e1 == e2);
        }
        // The same for component blocks, plus a real movable global block.
        // Each side gets its own block: the lock count is per block, so sharing
        // one would make the second unlock observe the first side's lock.
        HGLOBAL mine_block = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 64);
        HGLOBAL wine_block = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 64);
        CHECK(mine_block && wine_block);
        HIMCC handles[] = {HIMCC(nullptr), reinterpret_cast<HIMCC>(0x5678)};
        for (HIMCC handle : handles) {
            SetLastError(0xdead); void* mine = mine_lockcc(handle); DWORD e1 = GetLastError();
            SetLastError(0xdead); void* wine = wine_lockcc(handle); DWORD e2 = GetLastError();
            CHECK(mine == nullptr && wine == nullptr && e1 == e2);
            SetLastError(0xdead); BOOL ur = mine_unlockcc(handle); e1 = GetLastError();
            SetLastError(0xdead); BOOL wr = wine_unlockcc(handle); e2 = GetLastError();
            if (ur != wr || e1 != e2) std::printf("ImmUnlockIMCC mine=%d/%lu Wine=%d/%lu\n", ur, e1, wr, e2);
            CHECK(ur == wr && e1 == e2);
        }
        // A real block locks to a data pointer and unlocks TRUE, then FALSE.
        SetLastError(0xdead); void* mine = mine_lockcc(reinterpret_cast<HIMCC>(mine_block)); DWORD e1 = GetLastError();
        SetLastError(0xdead); void* wine = wine_lockcc(reinterpret_cast<HIMCC>(wine_block)); DWORD e2 = GetLastError();
        CHECK(mine && wine && e1 == e2);
        SetLastError(0xdead); BOOL ur = mine_unlockcc(reinterpret_cast<HIMCC>(mine_block)); e1 = GetLastError();
        SetLastError(0xdead); BOOL wr = wine_unlockcc(reinterpret_cast<HIMCC>(wine_block)); e2 = GetLastError();
        CHECK(ur == wr && e1 == e2);
        // Unlocking an unlocked block fails the same way on both sides.
        SetLastError(0xdead); ur = mine_unlockcc(reinterpret_cast<HIMCC>(mine_block)); e1 = GetLastError();
        SetLastError(0xdead); wr = wine_unlockcc(reinterpret_cast<HIMCC>(wine_block)); e2 = GetLastError();
        CHECK(ur == wr && e1 == e2);
        GlobalFree(mine_block); GlobalFree(wine_block);
        // Wine's stub returns FALSE and leaves the last error alone.
        SetLastError(0xdead); BOOL mine_disabled = mine_disable(0); e1 = GetLastError();
        SetLastError(0xdead); BOOL wine_disabled = wine_disable(0); e2 = GetLastError();
        CHECK(mine_disabled == wine_disabled && e1 == e2);
    }
    // The no-context/no-IME calls against Wine: same result and last error.
    {
        HKL english = reinterpret_cast<HKL>(static_cast<uintptr_t>(0x04090409));
        using Call = long long (*)(uintptr_t, HKL);
        auto compare = [&](const char* name, Call call) -> bool {
            SetLastError(0xdead); const long long mine = call(imm32_core::resolve("imm32.dll", name), english); const DWORD e1 = GetLastError();
            SetLastError(0xdead); const long long wine = call(reinterpret_cast<uintptr_t>(GetProcAddress(reference, name)), english); const DWORD e2 = GetLastError();
            const bool same = mine == wine && e1 == e2;
            if (!same) std::printf("%s mine=%llx/%lx Wine=%llx/%lx\n", name, (unsigned long long)mine, (unsigned long)e1, (unsigned long long)wine, (unsigned long)e2);
            return same;
        };
        CHECK(compare("ImmGetDefaultIMEWnd", [](uintptr_t f, HKL) { return (long long)(intptr_t)reinterpret_cast<decltype(&ImmGetDefaultIMEWnd)>(f)(nullptr); }));
        CHECK(compare("ImmGetIMEFileNameW", [](uintptr_t f, HKL l) { return (long long)reinterpret_cast<decltype(&ImmGetIMEFileNameW)>(f)(l, nullptr, 0); }));
        CHECK(compare("ImmGetOpenStatus", [](uintptr_t f, HKL) { return (long long)reinterpret_cast<decltype(&ImmGetOpenStatus)>(f)(nullptr); }));
        CHECK(compare("ImmSetOpenStatus", [](uintptr_t f, HKL) { return (long long)reinterpret_cast<decltype(&ImmSetOpenStatus)>(f)(nullptr, TRUE); }));
        CHECK(compare("ImmSetConversionStatus", [](uintptr_t f, HKL) { return (long long)reinterpret_cast<decltype(&ImmSetConversionStatus)>(f)(nullptr, 0, 0); }));
        CHECK(compare("ImmGetVirtualKey", [](uintptr_t f, HKL) { return (long long)reinterpret_cast<decltype(&ImmGetVirtualKey)>(f)(nullptr); }));
        // Wine gives every layout its built-in IME; this backend has none, so
        // ImmIsIME is FALSE here (the no-IME result), deliberately unlike Wine.
        SetLastError(0xdead);
        CHECK(!reinterpret_cast<decltype(&ImmIsIME)>(imm32_core::resolve("imm32.dll", "ImmIsIME"))(english) && GetLastError() == 0xdead);
    }
    FreeLibrary(reference);
    std::puts("IMM32 OFFDEVICE PASS eight exports, association regression, typed ABI, Wine null/foreign-context differential, context/component locks, errors, untouched outputs, resolver");
}
