#include "route.h"
#include <cstdio>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s error=%lu\n", __LINE__, #x, GetLastError()); return 1; } } while (0)
int main() {
    CHECK(!gdi32_core::resolve("gdi32.dll", "GetDeviceCaps"));
    HMODULE module = LoadLibraryW(L"uwph-u32c.dll");
    CHECK(module);
    auto device_caps = reinterpret_cast<gdi32_core::DeviceCaps>(
        GetProcAddress(module, "uwph_u32c_device_caps"));
    auto caps = reinterpret_cast<gdi32_core::DeviceCaps>(gdi32_core::resolve("GDI32.DLL", "GetDeviceCaps",device_caps));
    auto get_dc = reinterpret_cast<decltype(&GetDC)>(GetProcAddress(module, "GetDC"));
    auto release_dc = reinterpret_cast<decltype(&ReleaseDC)>(GetProcAddress(module, "ReleaseDC"));
    auto desktop = reinterpret_cast<decltype(&GetDesktopWindow)>(GetProcAddress(module, "GetDesktopWindow"));
    auto metrics = reinterpret_cast<decltype(&GetSystemMetrics)>(GetProcAddress(module, "GetSystemMetrics"));
    using Configure = void (WINAPI*)(LONG,LONG);
    auto configure = reinterpret_cast<Configure>(GetProcAddress(module, "uwph_u32c_configure"));
    CHECK(caps && get_dc && release_dc && desktop && metrics && configure);
    HMODULE oracle = LoadLibraryW(L"gdi32.dll");
    CHECK(oracle);
    auto reference = reinterpret_cast<gdi32_core::DeviceCaps>(GetProcAddress(oracle, "GetDeviceCaps"));
    CHECK(reference);
    unsigned invalid = 0;
    for (UINT_PTR value : {UINT_PTR(0),UINT_PTR(1),UINT_PTR(-1),UINT_PTR(0x123456789ULL)})
    for (int index : {-1,0,8,10,12,88,116,117,118,999}) {
        auto handle = reinterpret_cast<HDC>(value);
        SetLastError(0xdead);
        int expected = reference(handle,index);
        DWORD error = GetLastError();
        CHECK(!expected && error == ERROR_INVALID_HANDLE);
        SetLastError(0xdead);
        CHECK(caps(handle,index) == expected && GetLastError() == error);
        ++invalid;
    }
    HWND window = desktop();
    HDC first = get_dc(nullptr), second = get_dc(window);
    CHECK(window && first && second && first != second);
    for (int width : {1920,1280,1,2147483647}) {
        configure(width,720);
        for (HDC dc : {first,second}) {
            SetLastError(0xdead);
            CHECK(caps(dc,HORZRES) == metrics(SM_CXSCREEN));
            CHECK(caps(dc,VERTRES) == metrics(SM_CYSCREEN));
            CHECK(caps(dc,DESKTOPHORZRES) == width && caps(dc,DESKTOPVERTRES) == 720);
            CHECK(caps(dc,HORZSIZE) == int((width*254LL+480)/960));
            CHECK(caps(dc,VERTSIZE) == 191);
            CHECK(caps(dc,BITSPIXEL) == 32 && caps(dc,PLANES) == 1);
            CHECK(caps(dc,COLORRES) == 24 && caps(dc,NUMCOLORS) == -1);
            CHECK(caps(dc,LOGPIXELSX) == 96 && caps(dc,LOGPIXELSY) == 96);
            CHECK(caps(dc,VREFRESH) == 60 && caps(dc,TECHNOLOGY) == DT_RASDISPLAY);
            for(int index : {CURVECAPS,LINECAPS,POLYGONALCAPS,TEXTCAPS,CLIPCAPS,
                             RASTERCAPS,SHADEBLENDCAPS,COLORMGMTCAPS,-1,999})
                CHECK(!caps(dc,index));
            CHECK(GetLastError() == 0xdead);
        }
    }
    // Match Wine's driver-independent display values using a real off-device DC.
    HDC host = GetDC(nullptr);
    CHECK(host);
    for (int index : {DRIVERVERSION,TECHNOLOGY,PLANES,NUMBRUSHES,NUMPENS,
                     NUMMARKERS,NUMFONTS,PDEVICESIZE,ASPECTX,ASPECTY,ASPECTXY,
                     SIZEPALETTE,NUMRESERVED,PHYSICALWIDTH,PHYSICALHEIGHT,
                     PHYSICALOFFSETX,PHYSICALOFFSETY,SCALINGFACTORX,SCALINGFACTORY,
                     BLTALIGNMENT})
        CHECK(caps(first,index) == reference(host,index));
    CHECK(ReleaseDC(nullptr,host));
    CHECK(release_dc(nullptr,first));
    SetLastError(0xdead);
    CHECK(!caps(first,HORZRES) && GetLastError() == ERROR_INVALID_HANDLE);
    CHECK(caps(second,HORZRES) == 2147483647);
    CHECK(release_dc(window,second));
    CHECK(!gdi32_core::resolve("user32.dll","GetDeviceCaps"));
    CHECK(!gdi32_core::resolve("gdi32.dll","getdevicecaps"));
    CHECK(!gdi32_core::resolve("gdi32.dll","#1"));
    std::printf("GDI32 CAPS OFFDEVICE PASS %u Wine invalid-DC cases, guest DC lifetime, configured metrics, conservative capabilities, typed resolver\n",invalid);
}
