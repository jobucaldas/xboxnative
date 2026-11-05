// SPDX-License-Identifier: GPL-3.0-or-later
#include "../launch_status.h"
#include "../launch_button.h"
#include "../launch_arguments.h"
#include <cstring>
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
    for(const char* label:{"GUEST RUNNING","LAUNCHING...","RESTART REQUIRED","INSTALLING...","PLAY","INSTALL"}) {
        auto layout=launch_button::label_layout(std::strlen(label));
        int textWidth=static_cast<int>(std::strlen(label))*8*layout.scale;
        assert(layout.x>=launch_button::padding);
        assert(layout.x+textWidth<=launch_button::width-launch_button::padding);
        assert(layout.x*2+textWidth==launch_button::width);
        assert(layout.y>=28 && layout.y+8*layout.scale<=68);
        assert(layout.scale==(!std::strcmp(label,"RESTART REQUIRED")?4:5));
    }
    puts("LAUNCH-BUTTON-OK: every action label fits padding and stays centered");
    assert(!std::strcmp(launch_arguments::for_app(3405340),
        "-screen-fullscreen 0 -force-d3d11 -logFile vis-player.log"));
    // Queued titles (including the separate demo) get no assumed engine flags.
    for(std::uint32_t appid:{0u,480u,1007u,413410u,413420u,555950u,567640u,589120u,
                            282900u,351710u,353270u,387340u,460120u,632350u,774511u,
                            1037590u,983970u,1145360u,1154810u,3405339u,3405341u,UINT32_MAX})
        assert(!*launch_arguments::for_app(appid));
    puts("LAUNCH-ARGS-OK: exact Megabonk options; no title-specific defaults for other app IDs");
}
