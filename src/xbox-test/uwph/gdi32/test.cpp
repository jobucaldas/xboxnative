#include "route.h"
#include <cstdio>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
namespace {
// Stands in for a mapped OpenGL implementation's exports (Mesa's opengl32.dll under Play).
int WINAPI fake_choose(HDC, const PIXELFORMATDESCRIPTOR*) { return 7; }
BOOL WINAPI fake_set(HDC, int format, const PIXELFORMATDESCRIPTOR*) { return format == 7; }
int WINAPI fake_get(HDC) { return 7; }
int WINAPI fake_describe(HDC, int format, UINT, void*) { return format == 1 ? 3 : 0; }
BOOL WINAPI fake_swap(HDC) { return TRUE; }
uintptr_t fake_lookup(const char* name) {
    if (!strcmp(name, "wglChoosePixelFormat")) return reinterpret_cast<uintptr_t>(&fake_choose);
    if (!strcmp(name, "wglSetPixelFormat")) return reinterpret_cast<uintptr_t>(&fake_set);
    if (!strcmp(name, "wglGetPixelFormat")) return reinterpret_cast<uintptr_t>(&fake_get);
    if (!strcmp(name, "wglDescribePixelFormat")) return reinterpret_cast<uintptr_t>(&fake_describe);
    if (!strcmp(name, "wglSwapBuffers")) return reinterpret_cast<uintptr_t>(&fake_swap);
    return 0;
}
}
int main() {
    using Function = int (WINAPI*)(HGDIOBJ, int, void*);
    auto call = reinterpret_cast<Function>(gdi32_core::resolve("GDI32.DLL", "GetObjectA"));
    HMODULE oracle = LoadLibraryW(L"gdi32.dll");
    CHECK(call && oracle);
    auto reference = reinterpret_cast<Function>(GetProcAddress(oracle, "GetObjectA"));
    CHECK(reference);
    unsigned cases = 0;
    for (UINT_PTR value : {UINT_PTR(0), UINT_PTR(1), UINT_PTR(-1), UINT_PTR(0x123456789ULL)})
    for (int size : {-1, 0, 1, 16, 60, 0x7fffffff})
    for (bool null_output : {false, true}) {
        unsigned char buffer[128], expected[128];
        memset(buffer, 0xa5, sizeof(buffer));
        memcpy(expected, buffer, sizeof(buffer));
        auto handle = reinterpret_cast<HGDIOBJ>(value);
        SetLastError(0xdead);
        int result = reference(handle, size, null_output ? nullptr : expected);
        CHECK(result == 0 && GetLastError() == 0xdead);
        CHECK(call(handle, size, null_output ? nullptr : buffer) == result);
        CHECK(GetLastError() == 0xdead);
        CHECK(!memcmp(buffer, expected, sizeof(buffer)));
        for (unsigned char byte : buffer) CHECK(byte == 0xa5);
        ++cases;
    }
    CHECK(!gdi32_core::resolve(nullptr, "GetObjectA"));
    CHECK(!gdi32_core::resolve("gdi32.dll", nullptr));
    CHECK(!gdi32_core::resolve("user32.dll", "GetObjectA"));
    CHECK(!gdi32_core::resolve("gdi32.dll", "getobjecta"));
    CHECK(!gdi32_core::resolve("gdi32.dll", "#1"));
    CHECK(!gdi32_core::resolve("gdi32.dll", "CreatePen"));
    {   // Mesa's software winsys imports StretchDIBits: it resolves and fails cleanly (no GDI surface).
        auto stretch = reinterpret_cast<int (WINAPI*)(HDC,int,int,int,int,int,int,int,int,const void*,const void*,UINT,DWORD)>(
            gdi32_core::resolve("gdi32.dll", "StretchDIBits"));
        CHECK(stretch);
        SetLastError(0);
        CHECK(stretch(nullptr,0,0,1,1,0,0,1,1,nullptr,nullptr,0,SRCCOPY) == 0 && GetLastError() == ERROR_NOT_SUPPORTED);
    }  // unimplemented names stay unresolved
    auto set_format = reinterpret_cast<BOOL (WINAPI*)(HDC, int, const PIXELFORMATDESCRIPTOR*)>(
        gdi32_core::resolve("GDI32.DLL", "SetPixelFormat"));
    auto swap = reinterpret_cast<BOOL (WINAPI*)(HDC)>(
        gdi32_core::resolve("GDI32.DLL", "SwapBuffers"));
    CHECK(set_format && swap);
    auto choose = reinterpret_cast<int (WINAPI*)(HDC, const PIXELFORMATDESCRIPTOR*)>(
        gdi32_core::resolve("GDI32.DLL", "ChoosePixelFormat"));
    CHECK(choose);
    unsigned choose_cases = 0;
    // No formats means no matching format, independent of requested format.
    // Use readable descriptors: Wine tracing may inspect their fields.
    for (UINT_PTR value : {UINT_PTR(0), UINT_PTR(1), UINT_PTR(-1), UINT_PTR(0x123456789ULL)})
    for (DWORD flags : {DWORD(0), DWORD(PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER), DWORD(-1)})
    for (BYTE type : {BYTE(PFD_TYPE_RGBA), BYTE(PFD_TYPE_COLORINDEX), BYTE(255)}) {
        PIXELFORMATDESCRIPTOR request{};
        request.nSize = sizeof(request);
        request.nVersion = 1;
        request.dwFlags = flags;
        request.iPixelType = type;
        request.cColorBits = 32;
        request.cDepthBits = 24;
        request.cStencilBits = 8;
        auto before = request;
        SetLastError(0xdead);
        CHECK(choose(reinterpret_cast<HDC>(value), &request) == 0);
        CHECK(GetLastError() == 0xdead);
        CHECK(!memcmp(&request, &before, sizeof(request)));
        ++choose_cases;
    }
    CHECK(!gdi32_core::resolve("user32.dll", "ChoosePixelFormat"));
    CHECK(!gdi32_core::resolve("gdi32.dll", "choosepixelformat"));
    std::printf("GDI32 OFFDEVICE PASS %u null-driver format selection cases\n", choose_cases);
    // Wine's null OpenGL driver is the oracle for this backend, rather than
    // the host Wine display driver (which may expose real pixel formats).
    PIXELFORMATDESCRIPTOR descriptor{};
    descriptor.nSize = sizeof(descriptor);
    descriptor.nVersion = 1;
    for (UINT_PTR value : {UINT_PTR(0), UINT_PTR(1), UINT_PTR(-1), UINT_PTR(0x123456789ULL)}) {
        auto dc = reinterpret_cast<HDC>(value);
        for (int format : {-1, 0, 1, 0x7fffffff}) {
            for (auto pfd : {static_cast<const PIXELFORMATDESCRIPTOR*>(nullptr),
                             static_cast<const PIXELFORMATDESCRIPTOR*>(&descriptor),
                             reinterpret_cast<const PIXELFORMATDESCRIPTOR*>(UINT_PTR(1))}) {
                SetLastError(0xdead);
                CHECK(!set_format(dc, format, pfd));
                CHECK(GetLastError() == 0xdead);
            }
        }
        SetLastError(0xdead);
        CHECK(!swap(dc));
        CHECK(GetLastError() == 0xdead);
    }
    CHECK(descriptor.nSize == sizeof(descriptor) && descriptor.nVersion == 1);
    CHECK(!gdi32_core::resolve("user32.dll", "SetPixelFormat"));
    CHECK(!gdi32_core::resolve("gdi32.dll", "swapbuffers"));
    {   // Stock objects are stable, survive DeleteObject, and pens/palettes are not provided.
        auto stock = reinterpret_cast<HGDIOBJ (WINAPI*)(int)>(gdi32_core::resolve("gdi32.dll", "GetStockObject"));
        CHECK(stock);
        HGDIOBJ white = stock(WHITE_BRUSH);
        CHECK(white && stock(WHITE_BRUSH) == white && stock(BLACK_BRUSH) && stock(BLACK_BRUSH) != white);
        CHECK(stock(SYSTEM_FONT) && stock(DEFAULT_GUI_FONT) && stock(NULL_BRUSH));
        CHECK(gdi32_core::delete_object(white) && stock(WHITE_BRUSH) == white);
        CHECK(!stock(BLACK_PEN) && !stock(DEFAULT_PALETTE) && !stock(-1) && !stock(20));
        auto dc = gdi32_core::create_compatible_dc(nullptr);   // a new DC holds WHITE_BRUSH and SYSTEM_FONT
        CHECK(dc && gdi32_core::select_object(dc, stock(BLACK_BRUSH)) == white);
        CHECK(gdi32_core::select_object(dc, stock(DEFAULT_GUI_FONT)) == stock(SYSTEM_FONT) && gdi32_core::delete_dc(dc));
    }
    {
        using Gamma = BOOL (WINAPI*)(HDC, void*);
        auto get = reinterpret_cast<Gamma>(gdi32_core::resolve("gdi32.dll", "GetDeviceGammaRamp"));
        auto set = reinterpret_cast<Gamma>(gdi32_core::resolve("gdi32.dll", "SetDeviceGammaRamp"));
        CHECK(get && set);
        static WORD ramp[3][256];
        HDC record = reinterpret_cast<HDC>(UINT_PTR(0x1234));
        CHECK(get(record, ramp) && ramp[0][0] == 0 && ramp[1][128] == 128 * 257 && ramp[2][255] == 0xffff);
        SetLastError(0); CHECK(!get(nullptr, ramp) && GetLastError() == ERROR_INVALID_PARAMETER);
        SetLastError(0); CHECK(!set(record, ramp) && GetLastError() == ERROR_NOT_SUPPORTED);
        SetLastError(0); CHECK(!set(record, nullptr) && GetLastError() == ERROR_INVALID_PARAMETER);
    }
    {   // With a mapped OpenGL implementation, gdi32 forwards to its wgl* entry points.
        auto describe = reinterpret_cast<int (WINAPI*)(HDC, int, UINT, void*)>(gdi32_core::resolve("gdi32.dll", "DescribePixelFormat"));
        auto get_format = reinterpret_cast<int (WINAPI*)(HDC)>(gdi32_core::resolve("gdi32.dll", "GetPixelFormat"));
        CHECK(describe && get_format);
        gdi32_core::gl_lookup.store(&fake_lookup);
        PIXELFORMATDESCRIPTOR request{};
        HDC dc = reinterpret_cast<HDC>(UINT_PTR(0x1234));
        CHECK(choose(dc, &request) == 7 && set_format(dc, 7, nullptr) && !set_format(dc, 6, nullptr));
        CHECK(swap(dc) && get_format(dc) == 7 && describe(dc, 1, sizeof request, &request) == 3);
        gdi32_core::gl_lookup.store([](const char*) -> uintptr_t { return 0; });   // a module without these exports
        CHECK(choose(dc, &request) == 0 && !set_format(dc, 7, nullptr) && !swap(dc) && describe(dc, 1, sizeof request, &request) == 0);
        SetLastError(0); CHECK(get_format(dc) == 0 && GetLastError() == ERROR_INVALID_PIXEL_FORMAT);
        gdi32_core::gl_lookup.store(nullptr);
        CHECK(choose(dc, &request) == 0 && !swap(dc));
        std::puts("GDI32 OFFDEVICE PASS pixel format/SwapBuffers forward to a mapped OpenGL module and fail cleanly without one");
    }
    FreeLibrary(oracle);
    {   // The D3DKMT family wined3d's adapter setup calls (research/D3D9-PLAN.md, milestone 2).
        using OpenLuid = LONG (WINAPI*)(gdi32_core::kmt::OpenAdapterFromLuid*);
        using OpenName = LONG (WINAPI*)(gdi32_core::kmt::OpenAdapterFromGdiDisplayName*);
        using CreateDevice = LONG (WINAPI*)(gdi32_core::kmt::CreateDevice*);
        using DestroyDevice = LONG (WINAPI*)(const gdi32_core::kmt::DestroyDevice*);
        using CloseAdapter = LONG (WINAPI*)(const gdi32_core::kmt::CloseAdapter*);
        using SetOwner = LONG (WINAPI*)(const gdi32_core::kmt::SetVidPnSourceOwner*);
        using QueryMemory = LONG (WINAPI*)(gdi32_core::kmt::QueryVideoMemoryInfo*);
        auto open_luid = reinterpret_cast<OpenLuid>(gdi32_core::resolve("gdi32.dll", "D3DKMTOpenAdapterFromLuid"));
        auto open_name = reinterpret_cast<OpenName>(gdi32_core::resolve("gdi32.dll", "D3DKMTOpenAdapterFromGdiDisplayName"));
        auto create_device = reinterpret_cast<CreateDevice>(gdi32_core::resolve("gdi32.dll", "D3DKMTCreateDevice"));
        auto destroy_device = reinterpret_cast<DestroyDevice>(gdi32_core::resolve("gdi32.dll", "D3DKMTDestroyDevice"));
        auto close_adapter = reinterpret_cast<CloseAdapter>(gdi32_core::resolve("gdi32.dll", "D3DKMTCloseAdapter"));
        auto set_owner = reinterpret_cast<SetOwner>(gdi32_core::resolve("gdi32.dll", "D3DKMTSetVidPnSourceOwner"));
        auto query_memory = reinterpret_cast<QueryMemory>(gdi32_core::resolve("gdi32.dll", "D3DKMTQueryVideoMemoryInfo"));
        CHECK(open_luid && open_name && create_device && destroy_device && close_adapter && set_owner && query_memory);
        // wined3d_adapter_init: open by LUID, then one output per attached display.
        gdi32_core::kmt::OpenAdapterFromLuid luid{};
        luid.AdapterLuid = gdi32_core::kmt::kDisplayLuid;
        CHECK(open_luid(&luid) == gdi32_core::kmt::kSuccess && luid.hAdapter);
        // wined3d_output_init: the display name opens the same adapter and names source 0.
        gdi32_core::kmt::OpenAdapterFromGdiDisplayName named{};
        wcscpy_s(named.DeviceName, L"\\\\.\\DISPLAY1");
        CHECK(open_name(&named) == gdi32_core::kmt::kSuccess && named.hAdapter);
        CHECK(named.AdapterLuid.LowPart == gdi32_core::kmt::kDisplayLuid.LowPart &&
              named.AdapterLuid.HighPart == gdi32_core::kmt::kDisplayLuid.HighPart && named.VidPnSourceId == 0);
        wcscpy_s(named.DeviceName, L"\\\\.\\DISPLAY2");
        CHECK(open_name(&named) == gdi32_core::kmt::kUnsuccessful);   // one record display
        CHECK(open_name(nullptr) == gdi32_core::kmt::kInvalidParameter || open_name(nullptr) != gdi32_core::kmt::kSuccess);
        // A device needs a live adapter; destroying or closing an unknown handle fails.
        gdi32_core::kmt::CreateDevice device{};
        device.hAdapter = luid.hAdapter;
        CHECK(create_device(&device) == gdi32_core::kmt::kSuccess && device.hDevice);
        gdi32_core::kmt::CreateDevice orphan{};
        orphan.hAdapter = 0xdead;
        CHECK(create_device(&orphan) == gdi32_core::kmt::kInvalidParameter && !orphan.hDevice);
        gdi32_core::kmt::CloseAdapter close{};
        close.hAdapter = 0xdead;
        CHECK(close_adapter(&close) == gdi32_core::kmt::kInvalidParameter);
        gdi32_core::kmt::DestroyDevice destroy{};
        destroy.hDevice = 0xdead;
        CHECK(destroy_device(&destroy) == gdi32_core::kmt::kInvalidParameter);
        // Ownership: SHARED is refused (the CoreWindow owns the output), EXCLUSIVE is granted and released.
        UINT32 exclusive = gdi32_core::kmt::kOwnerExclusive, shared = gdi32_core::kmt::kOwnerShared;
        gdi32_core::kmt::SourceId source = 0, other_source = 1;
        gdi32_core::kmt::SetVidPnSourceOwner owner{};
        owner.hDevice = device.hDevice; owner.pType = &shared; owner.pVidPnSourceId = &source; owner.VidPnSourceCount = 1;
        CHECK(set_owner(&owner) == gdi32_core::kmt::kVidPnSourceInUse);
        owner.pType = &exclusive;
        CHECK(set_owner(&owner) == gdi32_core::kmt::kSuccess);
        owner.pVidPnSourceId = &other_source;
        CHECK(set_owner(&owner) == gdi32_core::kmt::kInvalidParameter);
        owner.pVidPnSourceId = &source; owner.VidPnSourceCount = 0; owner.pType = nullptr;
        CHECK(set_owner(&owner) == gdi32_core::kmt::kSuccess);        // release
        owner.VidPnSourceCount = 1;
        CHECK(set_owner(&owner) == gdi32_core::kmt::kInvalidParameter);  // count without arrays
        // Video memory: the local segment reports this machine's memory, non-local reports none.
        gdi32_core::kmt::QueryVideoMemoryInfo memory{};
        memory.hAdapter = luid.hAdapter;
        memory.MemorySegmentGroup = gdi32_core::kmt::kMemoryLocal;
        CHECK(query_memory(&memory) == gdi32_core::kmt::kSuccess && memory.Budget > 0 &&
              memory.CurrentUsage <= memory.Budget && memory.AvailableForReservation == memory.Budget / 2);
        memory.MemorySegmentGroup = gdi32_core::kmt::kMemoryNonLocal;
        CHECK(query_memory(&memory) == gdi32_core::kmt::kSuccess && memory.Budget == 0);
        memory.MemorySegmentGroup = 7;
        CHECK(query_memory(&memory) == gdi32_core::kmt::kInvalidParameter);
        // A memory DC wraps caller pixels: the caller's data is what the DC reads and writes.
        using CreateDc = LONG (WINAPI*)(gdi32_core::kmt::CreateDcFromMemory*);
        using DestroyDc = LONG (WINAPI*)(const gdi32_core::kmt::DestroyDcFromMemory*);
        auto create_dc = reinterpret_cast<CreateDc>(gdi32_core::resolve("gdi32.dll", "D3DKMTCreateDCFromMemory"));
        auto destroy_dc = reinterpret_cast<DestroyDc>(gdi32_core::resolve("gdi32.dll", "D3DKMTDestroyDCFromMemory"));
        CHECK(create_dc && destroy_dc);
        static DWORD pixels[4 * 4] = {0};
        gdi32_core::kmt::CreateDcFromMemory dc{};
        dc.pMemory = pixels; dc.Format = gdi32_core::kmt::kFmtX8R8G8B8;
        dc.Width = 4; dc.Height = 4; dc.Pitch = 16; dc.hDeviceDc = gdi32_core::create_compatible_dc(nullptr);
        CHECK(create_dc(&dc) == gdi32_core::kmt::kSuccess && dc.hDc && dc.hBitmap);
        CHECK(gdi32_core::set_pixel(dc.hDc, 1, 2, RGB(1, 2, 3)) != COLORREF(-1));
        CHECK(pixels[2 * 4 + 1] == 0x00010203);   // top-down rows, R in the high byte of BGRX
        gdi32_core::kmt::DestroyDcFromMemory gone{dc.hDc, dc.hBitmap};
        CHECK(destroy_dc(&gone) == gdi32_core::kmt::kSuccess);
        CHECK(!gdi32_core::any(dc.hDc) && !gdi32_core::any((HGDIOBJ)dc.hBitmap));
        CHECK(destroy_dc(&gone) == gdi32_core::kmt::kInvalidParameter);   // already destroyed
        gdi32_core::delete_dc(dc.hDeviceDc);
        // Rejections Wine's win32u makes: no memory, unknown format, pitch below the DIB stride, no device DC.
        gdi32_core::kmt::CreateDcFromMemory bad{};
        bad.pMemory = pixels; bad.Format = gdi32_core::kmt::kFmtX8R8G8B8;
        bad.Width = 4; bad.Height = 4; bad.Pitch = 16; bad.hDeviceDc = gdi32_core::create_compatible_dc(nullptr);
        auto device_dc = bad.hDeviceDc;
        bad.pMemory = nullptr;
        CHECK(create_dc(&bad) == gdi32_core::kmt::kInvalidParameter);
        bad.pMemory = pixels; bad.Format = 0x99;
        CHECK(create_dc(&bad) == gdi32_core::kmt::kInvalidParameter);
        bad.Format = gdi32_core::kmt::kFmtX8R8G8B8; bad.Pitch = 8;   // 4 px * 32 bpp needs 16
        CHECK(create_dc(&bad) == gdi32_core::kmt::kInvalidParameter);
        bad.Pitch = 16; bad.hDeviceDc = nullptr;
        CHECK(create_dc(&bad) == gdi32_core::kmt::kInvalidParameter);
        gdi32_core::delete_dc(device_dc);
        // The two remaining GDI imports: a 32-bpp bitmap has no colour table, and there is no
        // drawing surface for StretchBlt.
        auto set_colors = reinterpret_cast<UINT (WINAPI*)(HDC, UINT, UINT, const RGBQUAD*)>(
            gdi32_core::resolve("gdi32.dll", "SetDIBColorTable"));
        auto stretch = reinterpret_cast<BOOL (WINAPI*)(HDC, int, int, int, int, HDC, int, int, int, int, DWORD)>(
            gdi32_core::resolve("gdi32.dll", "StretchBlt"));
        CHECK(set_colors && stretch);
        CHECK(set_colors(nullptr, 0, 256, nullptr) == 0);
        SetLastError(0);
        CHECK(!stretch(nullptr, 0, 0, 1, 1, nullptr, 0, 0, 1, 1, SRCCOPY) && GetLastError() == ERROR_NOT_SUPPORTED);
        { gdi32_core::kmt::DestroyDevice d{device.hDevice}; CHECK(destroy_device(&d) == gdi32_core::kmt::kSuccess); }
        { gdi32_core::kmt::CloseAdapter c{luid.hAdapter}; CHECK(close_adapter(&c) == gdi32_core::kmt::kSuccess); }
        { gdi32_core::kmt::CloseAdapter c{named.hAdapter}; CHECK(close_adapter(&c) == gdi32_core::kmt::kSuccess); }
        std::puts("GDI32 OFFDEVICE PASS D3DKMT adapter/device/ownership/memory/DC and the remaining wined3d imports");
    }
    std::puts("GDI32 OFFDEVICE PASS null-driver pixel format/presentation failures, descriptor guards, last error");
    std::printf("GDI32 OFFDEVICE PASS %u invalid-object cases, Wine differential, output guards, last error, resolver isolation\n", cases);
}
