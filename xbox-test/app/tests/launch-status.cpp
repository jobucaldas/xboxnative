// SPDX-License-Identifier: GPL-3.0-or-later
#include "../launch_status.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    using namespace launch_status;
    assert(observe(false,false,0,false)==Outcome::starting);
    assert(observe(false,false,0,true)==Outcome::timedOut);
    for(int milestone=0;milestone<=2;++milestone) {
        for(bool expired:{false,true}) {
            assert(observe(true,false,milestone,expired)==Outcome::failed);
            assert(observe(true,true,milestone,expired)==Outcome::completed);
            if(milestone) {
                assert(observe(false,false,milestone,expired)==Outcome::running);
                assert(!terminal(observe(false,true,milestone,expired)));
            }
        }
    }
    assert(!terminal(Outcome::starting));assert(!terminal(Outcome::running));
    assert(terminal(Outcome::completed));assert(terminal(Outcome::failed));assert(terminal(Outcome::timedOut));
    puts("LAUNCH-STATUS-OK: progress is not success; terminal failure wins");
}
