// SPDX-License-Identifier: GPL-3.0-or-later
// Contract test for the opt-in D3D11 call ring: it keeps the newest hooked
// calls, preserves their order, and emits them when a failed Present flushes it.
#include "../host/d3d11_diag.h"
#include <cstdio>
#include <string>
#include <vector>

static std::vector<std::string> lines;
static void collect(const char* line){lines.emplace_back(line);}
static unsigned checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL line %d: %s\n",__LINE__,#x);} } while(0)

int main() {
    d3d11_diag::sink=collect;
    d3d11_diag::traceOn.store(true);
    d3d11_diag::traceNextSeq=1;
    for(auto& e:d3d11_diag::traceRing)e={};
    d3d11_diag::traceCall("ResizeBuffers #1");
    d3d11_diag::traceCall("Present");
    d3d11_diag::traceCall("ResizeBuffers #2");
    d3d11_diag::flushTrace();
    CHECK(lines.size()==4);
    CHECK(lines.size()>1&&lines[1].find("#1 ResizeBuffers #1")!=std::string::npos);
    CHECK(lines.size()>2&&lines[2].find("#2 Present")!=std::string::npos);
    CHECK(lines.size()>3&&lines[3].find("#3 ResizeBuffers #2")!=std::string::npos);

    lines.clear();
    for(unsigned i=0;i<d3d11_diag::kTraceRing+2;++i)d3d11_diag::traceCall("Draw");
    d3d11_diag::flushTrace();
    CHECK(lines.size()==d3d11_diag::kTraceRing+1);
    CHECK(lines.size()>1&&lines[1].find("#6 Draw")!=std::string::npos);
    CHECK(lines.size()>64&&lines.back().find("#69 Draw")!=std::string::npos);

    d3d11_diag::traceOn.store(false);
    lines.clear();d3d11_diag::traceCall("not recorded");d3d11_diag::flushTrace();
    CHECK(lines.empty());
    if(failures){printf("d3d11-resize-trace: %u checks, %u FAIL\n",checks,failures);return 1;}
    printf("d3d11-resize-trace: %u checks — ordered bounded ring, opt-in gate PASS\n",checks);
    return 0;
}
