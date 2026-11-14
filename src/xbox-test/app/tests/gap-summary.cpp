// SPDX-License-Identifier: GPL-3.0-or-later
#include "../gap_summary.h"
#include <cstdio>

static int failures;
#define CHECK(x) do { if(!(x)) { ++failures; std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while(0)

static void fixture(const char* arch) {
    gap_summary::Binder binder;
    binder.record("kernel32.dll","GetTickCount",gap_summary::Outcome::routed);
    binder.record("dbghelp.dll","SymInitialize",gap_summary::Outcome::stub);
    binder.record("hid.dll","HidD_GetAttributes",gap_summary::Outcome::donor);
    binder.record("user32.dll","GetProcessWindowStation",gap_summary::Outcome::resolved);
    binder.record("z-missing.dll","Absent",gap_summary::Outcome::missing);
    binder.record("z-missing.dll","Absent",gap_summary::Outcome::missing);
    auto report=binder.report("fixture.exe",arch);
    CHECK(report.lines.size()==7);
    CHECK(report.lines[0]=="GAP: dll=dbghelp.dll resolved=0 routed=0 donor=0 stub=1 missing=0");
    CHECK(report.lines[1]=="GAP: dll=hid.dll resolved=0 routed=0 donor=1 stub=0 missing=0");
    CHECK(report.lines[2]=="GAP: dll=kernel32.dll resolved=0 routed=1 donor=0 stub=0 missing=0");
    CHECK(report.lines[3]=="GAP: dll=user32.dll resolved=1 routed=0 donor=0 stub=0 missing=0");
    CHECK(report.lines[4]=="GAP: dll=z-missing.dll resolved=0 routed=0 donor=0 stub=0 missing=1");
    CHECK(report.lines[5]==std::string("GAP: summary exe=fixture.exe arch=")+arch+" resolved=1 routed=1 donor=1 stub=1 missing=1");
    CHECK(report.lines[6]=="GAP: missing z-missing.dll!Absent");
}

int main() {
    fixture("x64");
    fixture("x86");
    std::printf("gap-summary: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
