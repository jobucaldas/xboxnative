// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the redundant-resize elision decision (host/resize_elide.h).
// Pure functions, no Windows headers: runs on the host toolchain. The cases
// come from the Slime Rancher run1 resize sequence (0.5.303.0): #3/#4 requested
// the chain's current mode and #4 returned DEVICE_REMOVED.
#include "../host/resize_elide.h"
#include <cstdio>

static unsigned checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL line %d: %s\n",__LINE__,#x);} } while(0)

using resize_elide::Mode;
using resize_elide::same_mode;
using resize_elide::enabled;

int main() {
    // The chain as Slime Rancher's ran: 1920x1080, 2 buffers (real desc),
    // fmt 0x1c (B8G8R8A8_UNORM), flags 0x842 minus the waitable bit the hook
    // strips = 0x802 as the chain reports it. Note the normalisation raises a
    // real count below 3 to 3, so on a 2-buffer chain only "keep" (0) can
    // match; the console chain is created with 3 buffers.
    const Mode chain{2,1920,1080,0x1c,0x802};

    // 1. A real count request below 3 is raised to 3: differs from the current
    //    2, so forwarded. On the console's 3-buffer chain, 2 forwards too.
    CHECK(!same_mode(chain,{2,1920,1080,0x1c,0x842}));

    // 2. Any real difference is forwarded, one field at a time.
    CHECK(!same_mode(chain,{0,1280,720,0x1c,0x842}));    // width/height (run1 #1 shape)
    CHECK(!same_mode(chain,{3,1920,1080,0x1c,0x842}));   // count 3 vs current 2
    CHECK(!same_mode(chain,{0,1920,1080,0x17,0x842}));   // format
    CHECK(!same_mode(chain,{0,1920,1080,0x1c,0x843}));   // a flag that survives the strip

    // 3. BufferCount 0 = keep, and it is the only way to match a 2-buffer chain.
    CHECK(same_mode(chain,{0,1920,1080,0x1c,0x842}));
    //    On a 3-buffer chain (the console's): keep matches, a real 3 matches,
    //    higher counts forward.
    const Mode chain3{3,1920,1080,0x1c,0x802};
    CHECK(same_mode(chain3,{0,1920,1080,0x1c,0x842}));   // keep = 3 = current
    CHECK(same_mode(chain3,{3,1920,1080,0x1c,0x842}));   // 3 -> max(3,3) = 3 = current
    CHECK(!same_mode(chain3,{4,1920,1080,0x1c,0x842}));  // 4 -> max(4,3) = 4, differs
    CHECK(!same_mode(chain3,{5,1920,1080,0x1c,0x842}));

    // 4. DXGI_FORMAT_UNKNOWN (0) = keep the current format.
    CHECK(same_mode(chain,{0,1920,1080,0,0x842}));
    CHECK(!same_mode(chain,{0,1920,1080,0,0x843}));

    // 5. The waitable-flag bit (0x40) is stripped before comparing, both when
    //    the request adds it and when the current desc somehow carries it.
    CHECK(same_mode(chain,{0,1920,1080,0x1c,0x842}));    // 0x842 & ~0x40 = 0x802
    const Mode chainWaitable{2,1920,1080,0x1c,0x842};    // current carries it too
    CHECK(same_mode(chainWaitable,{0,1920,1080,0x1c,0x802}));
    CHECK(same_mode(chainWaitable,{0,1920,1080,0x1c,0x842}));

    // 6. Opt-out parsing: default ON; a lowercase "off" anywhere disables;
    //    anything else (empty, "on", whitespace, "OFF") keeps it on — the
    //    comparison is the exact substring, matching the launcher's other
    //    content-based file reads.
    CHECK(enabled(""));
    CHECK(enabled("on"));
    CHECK(enabled("\n"));
    CHECK(enabled("OFF"));
    CHECK(!enabled("off"));
    CHECK(!enabled("elide=off\n"));

    if(failures){printf("resize-elision: %u checks, %u FAIL\n",checks,failures);return 1;}
    printf("resize-elision: %u checks — same-mode elision, keep normalisation, flag strip, opt-out PASS\n",checks);
    return 0;
}
