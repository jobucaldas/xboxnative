// SPDX-License-Identifier: GPL-3.0-or-later
// i686 fixture for the x86 guest's GDI memory-DC surface.
#include <windows.h>

static void fail(unsigned code) { ExitProcess(code); }
static LRESULT CALLBACK gdi_window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) { (void)window; (void)message; (void)wp; (void)lp; return 0; }
void WINAPI entry(void) {
    WNDCLASSEXA wc = {0}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=gdi_window_proc; wc.lpszClassName="X86GDIWindow";
    if (!RegisterClassExA(&wc)) fail(1);
    HWND window=CreateWindowExA(0,wc.lpszClassName,"GDI test",WS_OVERLAPPEDWINDOW,0,0,32,32,NULL,NULL,NULL,NULL);
    HDC window_dc=window?GetDC(window):NULL;
    if (!window_dc) fail(1);
    HDC src = CreateCompatibleDC(NULL), dst = CreateCompatibleDC(NULL);
    if (!src || !dst) fail(2);
    if (GetDeviceCaps(window_dc, HORZRES) != 1280 || GetDeviceCaps(src, VERTRES) != 720 ||
        GetDeviceCaps(src, BITSPIXEL) != 32 || GetDeviceCaps(src, PLANES) != 1 ||
        GetDeviceCaps(src, LOGPIXELSX) != 96 || GetDeviceCaps(src, LOGPIXELSY) != 96 ||
        GetDeviceCaps(src, VREFRESH) != 60 || GetDeviceCaps(src, RASTERCAPS) != 0) fail(1);
    BITMAPINFO info = {0}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 2; info.bmiHeader.biHeight = -2; info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void *src_bits = NULL, *dst_bits = NULL;
    HBITMAP sb = CreateDIBSection(src, &info, DIB_RGB_COLORS, &src_bits, NULL, 0);
    HBITMAP db = CreateDIBSection(dst, &info, DIB_RGB_COLORS, &dst_bits, NULL, 0);
    if (!sb || !db || !src_bits || !dst_bits) fail(3);
    struct { BITMAPINFOHEADER header; DWORD masks[3]; } bitfields = {0};
    bitfields.header = info.bmiHeader; bitfields.header.biCompression = BI_BITFIELDS;
    bitfields.masks[0] = 0x00ff0000; bitfields.masks[1] = 0x0000ff00; bitfields.masks[2] = 0x000000ff;
    void *bitfield_bits = NULL;
    HBITMAP bitfield = CreateDIBSection(src, (BITMAPINFO *)&bitfields, DIB_RGB_COLORS, &bitfield_bits, NULL, 0);
    if (!bitfield || !bitfield_bits) fail(13);
    struct { BITMAPINFOHEADER header; DWORD masks[3]; } query = {0};
    query.header.biSize = sizeof(BITMAPINFOHEADER);
    if (GetDIBits(src, bitfield, 0, 0, NULL, (BITMAPINFO *)&query, DIB_RGB_COLORS) != 0 ||
        query.header.biCompression != BI_BITFIELDS || query.masks[0] != 0x00ff0000 ||
        query.masks[1] != 0x0000ff00 || query.masks[2] != 0x000000ff) fail(14);
    ((DWORD *)src_bits)[0] = 0x00112233; ((DWORD *)src_bits)[1] = 0x00445566;
    HBITMAP old_src = SelectObject(src, sb), old_dst = SelectObject(dst, db);
    if (!old_src || !old_dst || !BitBlt(dst, 0, 0, 2, 2, src, 0, 0, SRCCOPY)) fail(4);
    if (BitBlt(window_dc,0,0,2,2,src,0,0,SRCCOPY)) fail(4); // no GDI window presenter: never claim success
    if (((DWORD *)dst_bits)[0] != 0x00112233 || ((DWORD *)dst_bits)[1] != 0x00445566) fail(5);
    BITMAP bm = {0}; if (GetObjectA(db, sizeof(bm), &bm) != sizeof(bm) || bm.bmWidth != 2 || bm.bmHeight != 2) fail(6);
    BYTE mask_bits[8] = {0x80,0,0,0,0x40,0,0,0};
    HBITMAP mask = CreateBitmap(8, 2, 1, 1, mask_bits);
    BITMAP mask_bm = {0};
    if (!mask) fail(17);
    ICONINFO icon_info = {0}; icon_info.hbmMask = mask; icon_info.hbmColor = bitfield;
    HICON icon = CreateIconIndirect(&icon_info);
    HICON copy = (HICON)CopyImage(icon, IMAGE_CURSOR, 8, 2, 0);
    if (!icon || !copy || !DestroyIcon(copy) || !DestroyIcon(icon)) fail(26);
    if (GetObjectA(mask, sizeof(mask_bm), &mask_bm) != sizeof(mask_bm)) fail(18);
    if (mask_bm.bmWidth != 8) fail(21);
    if (mask_bm.bmHeight != 2) fail(22);
    if (mask_bm.bmWidthBytes != 4) fail(23);
    if (mask_bm.bmPlanes != 1) fail(24);
    if (mask_bm.bmBitsPixel != 1) fail(25);
    if (!DeleteObject(mask)) fail(20);
    HBITMAP compatible = CreateCompatibleBitmap(dst, 3, 3);
    if (!compatible || !DeleteObject(compatible)) fail(7);
    HBRUSH brush = CreateSolidBrush(RGB(1,2,3)); HPEN pen = CreatePen(PS_SOLID, 1, RGB(4,5,6));
    HBRUSH old_brush = SelectObject(dst, brush); HPEN old_pen = SelectObject(dst, pen);
    LOGBRUSH lb = {0};
    if (!brush || !pen || !old_brush || !old_pen || GetObjectA(brush, sizeof(lb), &lb) != (int)sizeof(lb) ||
        lb.lbColor != RGB(1,2,3) || !GetStockObject(WHITE_BRUSH) || !SetBkMode(dst, TRANSPARENT) ||
        SetTextColor(dst, RGB(1,2,3)) == CLR_INVALID || SetBkColor(dst, RGB(4,5,6)) == CLR_INVALID || !GdiFlush()) fail(8);
    if (!StretchBlt(dst,0,0,2,2,src,0,0,2,2,SRCCOPY) || ((DWORD *)dst_bits)[1] != 0x00445566) fail(9);
    if (StretchDIBits(dst,0,0,2,2,0,0,2,2,src_bits,&info,DIB_RGB_COLORS,SRCCOPY) != (int)GDI_ERROR ||
        SetDIBitsToDevice(dst,0,0,2,2,0,0,0,2,src_bits,&info,DIB_RGB_COLORS) != (int)GDI_ERROR) fail(10);
    if (GetObjectW(db, sizeof(bm), &bm) != sizeof(bm)) fail(11);
    if (!SelectObject(dst, old_brush) || !SelectObject(dst, old_pen) || !SelectObject(src, old_src) || !SelectObject(dst, old_dst) || !DeleteObject(sb) || !DeleteObject(db) || !DeleteObject(bitfield) || !DeleteObject(brush) || !DeleteObject(pen) || !DeleteDC(src) || !DeleteDC(dst) || !ReleaseDC(window,window_dc) || !DestroyWindow(window)) fail(12);
    if (CreateFontIndirectW(NULL) != NULL || CreateFontIndirectA(NULL) != NULL ||
        CreateFontW(0,0,0,0,0,0,0,0,0,0,0,0,0,0) != NULL ||
        CreateFontA(0,0,0,0,0,0,0,0,0,0,0,0,0,0) != NULL || GetTextMetricsW(NULL, NULL) ||
        GetTextMetricsA(NULL, NULL) || GetTextExtentPoint32W(NULL, L"x", 1, NULL) ||
        GetTextExtentPoint32A(NULL, "x", 1, NULL) || TextOutW(NULL,0,0,L"x",1) || TextOutA(NULL,0,0,"x",1)) fail(13);
    ExitProcess(0);
}
