/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real PE32 guest covering the x86 user32 rectangle ABI. */
#include <windows.h>
static int checks, failures;
static void check(int ok) { ++checks; if (!ok) ++failures; }
void __stdcall entry(void) {
    RECT r;
    check(SetRect(&r, 10, 20, 40, 60));
    check(r.left == 10 && r.top == 20 && r.right == 40 && r.bottom == 60);
    RECT copy;
    check(CopyRect(&copy, &r));
    check(EqualRect(&copy, &r));
    check(OffsetRect(&copy, 2, 3));
    check(copy.left == 12 && copy.top == 23 && copy.right == 42 && copy.bottom == 63);
    check(InflateRect(&copy, 1, 1));
    check(copy.left == 11 && copy.top == 22 && copy.right == 43 && copy.bottom == 64);
    RECT out;
    check(IntersectRect(&out, &r, &copy));
    check(out.left == 11 && out.top == 22 && out.right == 40 && out.bottom == 60);
    check(UnionRect(&out, &r, &copy));
    check(out.left == 10 && out.top == 20 && out.right == 43 && out.bottom == 64);
    POINT p = {10, 20};
    check(PtInRect(&r, p));
    p.x = 40; p.y = 60;
    check(!PtInRect(&r, p));
    SetRectEmpty(&r);
    check(IsRectEmpty(&r));
    check(GetSystemMetrics(SM_CXSCREEN) == 1280);
    check(GetSystemMetrics(SM_CYSCREEN) == 720);
    ExitProcess(failures ? 1 : (checks == 17 ? 42 : 2));
    for (;;) {}
}
