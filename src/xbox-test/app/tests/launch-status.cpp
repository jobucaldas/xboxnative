// SPDX-License-Identifier: GPL-3.0-or-later
#include "../launch_status.h"
#include "../launch_button.h"
#include "../launch_target.h"
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
    // Launch options come from the app's own appinfo, as the Steam client picks them.
    Vdf info;
    assert(vdf_parse(R"("appinfo" { "config" { "launch" {
        "0" { "executable" "Game32.exe" "type" "default" "config" { "oslist" "windows" "osarch" "32" } }
        "1" { "executable" "x64/Game.exe" "arguments" "-steam" "type" "default" "config" { "oslist" "windows" "osarch" "64" } }
        "2" { "executable" "Game.app" "config" { "oslist" "macos" } }
        "3" { "executable" "Beta.exe" "type" "default" "config" { "oslist" "windows" "osarch" "64" "betakey" "beta" } }
        "4" { "executable" "Editor.exe" "type" "editor" "config" { "oslist" "windows" "osarch" "64" } }
        "5" { "executable" "Server.exe" "type" "server" }
    } } })",info));
    auto options=launch_target::entries(*info.get("appinfo"));
    assert(options.size()==6);
    auto chosen=launch_target::choose(options);
    assert(chosen && chosen->executable=="x64/Game.exe");
    std::string rel;
    assert(launch_target::relative_path(chosen->executable,rel) && rel=="x64\\Game.exe");
    assert(launch_target::arguments(chosen,false)=="-steam");
    assert(launch_target::arguments(chosen,true)=="-steam -screen-fullscreen 0 -force-d3d11 -logFile vis-player.log");
    // Megabonk (Unity, no Steam arguments) keeps its exact options, now from its files.
    assert(launch_target::arguments(nullptr,true)=="-screen-fullscreen 0 -force-d3d11 -logFile vis-player.log");
    // A non-Unity game gets no assumed engine options.
    assert(launch_target::arguments(nullptr,false).empty());
    // Unspecified OS/arch is accepted; only non-Windows options are skipped.
    Vdf plain;
    assert(vdf_parse(R"("config" { "launch" { "0" { "executable" "mac.app" "config" { "oslist" "macos,linux" } } "1" { "executable" ".\\Run.exe" } } })",plain));
    auto any=launch_target::entries(plain);
    assert(launch_target::choose(any)==&any[1]);
    assert(launch_target::relative_path(any[1].executable,rel) && rel=="Run.exe");
    // A "none"-typed Play entry beats an option1 alternative (safe mode etc.).
    Vdf typed;
    assert(vdf_parse(R"("config" { "launch" { "0" { "executable" "Safe.exe" "type" "option1" } "1" { "executable" "Game.exe" "type" "none" } } })",typed));
    auto typedOptions=launch_target::entries(typed);
    assert(launch_target::choose(typedOptions)==&typedOptions[1]);
    Vdf none;
    assert(vdf_parse(R"("config" { "launch" { "0" { "executable" "Tool.exe" "type" "server" } } })",none));
    auto tools=launch_target::entries(none);
    assert(!launch_target::choose(tools));
    for(const char* bad:{"","..\\Escape.exe","dir\\..\\x.exe","C:\\Game.exe","\\Game.exe","dir\\\\x.exe"})
        assert(!launch_target::relative_path(bad,rel));
    puts("LAUNCH-ARGS-OK: Steam launch option selection; Unity options from files, none assumed by app ID");
}
