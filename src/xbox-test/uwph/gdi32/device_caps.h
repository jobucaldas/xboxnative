#pragma once

namespace gdi32_core {
// GetDeviceCaps indices are the documented wingdi.h ABI. Use numbers because
// the UWP SDK hides desktop GDI declarations. Values describe our existing
// 32-bit, 96-DPI, 60-Hz virtual display, not the physical Xbox output.
// Wine 10 win32u/driver.c supplies display and color relationships. Rendering
// capability bits remain zero: these DC records have no drawing backend.
inline int display_cap(int cap, int width, int height) {
    switch (cap) {
    case 0: return 0x4000;                       // DRIVERVERSION
    case 2: return 1;                            // TECHNOLOGY: DT_RASDISPLAY
    case 4: return int((width * 254LL + 480) / 960); // HORZSIZE, nearest mm
    case 6: return int((height * 254LL + 480) / 960);// VERTSIZE
    case 8: case 118: return width;              // HORZRES, DESKTOPHORZRES
    case 10: case 117: return height;            // VERTRES, DESKTOPVERTRES
    case 12: return 32;                          // BITSPIXEL
    case 14: return 1;                           // PLANES
    case 16: case 18: case 24: return -1;         // NUMBRUSHES/PENS/COLORS
    case 40: case 42: return 36;                 // ASPECTX/Y
    case 44: return 51;                          // ASPECTXY
    case 88: case 90: return 96;                 // LOGPIXELSX/Y
    case 106: return 20;                         // NUMRESERVED (Wine default)
    case 108: return 24;                         // COLORRES (32-bit display)
    case 116: return 60;                         // VREFRESH
    default: return 0;                           // no advertised rendering
    }
}
}
