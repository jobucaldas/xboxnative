#include "route.h"
#include <cstdio>
#include <array>
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
    FreeLibrary(reference);
    std::puts("IMM32 OFFDEVICE PASS eight exports, association regression, typed ABI, Wine null/foreign-context differential, errors, untouched outputs, resolver");
}
