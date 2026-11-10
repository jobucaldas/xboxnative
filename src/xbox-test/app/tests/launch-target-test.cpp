// SPDX-License-Identifier: GPL-3.0-or-later
// Launch option selection and the Play picker: both architectures are
// startable (64-bit natively, 32-bit in the in-process x86 interpreter), so a
// 64-bit option still wins over a 32-bit one typed "default" (Hades ships
// both), and a game with several run options must not pick one silently.
#include "../launch_target.h"
#include "../launch_picker.h"
#include <cassert>
#include <cstdio>
#include <map>

int main() {
    using namespace launch_target;
    // Hades (1145360) appinfo shape: x64 is option1, x86 is default, and the
    // Debug entries are beta-keyed.
    Vdf hades;
    assert(vdf_parse(R"("appinfo" { "config" { "launch" {
        "0" { "executable" "x64\\Hades.exe" "type" "option1" "description" "DirectX" "config" { "oslist" "windows" "osarch" "64" } }
        "1" { "executable" "x86\\Hades.exe" "type" "default" "description" "32-bit" "config" { "oslist" "windows" "osarch" "32" } }
        "2" { "executable" "Debug\\x64\\Hades.exe" "type" "default" "config" { "oslist" "windows" "osarch" "64" "betakey" "nightly-qa" } }
        "3" { "executable" "Debug\\x86\\Hades.exe" "type" "default" "config" { "oslist" "windows" "osarch" "32" "betakey" "nightly-qa" } }
    } } })",hades));
    auto options=entries(*hades.get("appinfo"));
    assert(options.size()==4);
    assert(options[0].description=="DirectX");
    auto chosen=choose(options);
    assert(chosen && chosen->executable=="x64\\Hades.exe");
    std::string rel;
    assert(relative_path(chosen->executable,rel) && rel=="x64\\Hades.exe");
    // Every usable option in preference order, so an uninstalled best option
    // falls through to the next one.
    auto order=ranked(options);
    assert(order.size()==2 && order[0]->executable=="x64\\Hades.exe" && order[1]->executable=="x86\\Hades.exe");

    // With no architecture stated, the executable's PE header decides.
    Vdf unstated;
    assert(vdf_parse(R"("config" { "launch" {
        "0" { "executable" "Game32.exe" "type" "default" }
        "1" { "executable" "Game64.exe" "type" "option1" }
    } })",unstated));
    auto unstatedOptions=entries(unstated);
    std::map<std::string,int> pe{{"Game32.exe",arch32},{"Game64.exe",arch64}};
    auto byPe=choose(unstatedOptions,[&](const std::string& exe){return pe.count(exe)?pe[exe]:archUnknown;});
    assert(byPe && byPe->executable=="Game64.exe");
    // Without the PE probe the appinfo order and type decide, as before.
    assert(choose(unstatedOptions)==&unstatedOptions[0]);

    // Only 32-bit options: today's behaviour (the "default" one) is kept.
    Vdf only32;
    assert(vdf_parse(R"("config" { "launch" {
        "0" { "executable" "Safe.exe" "type" "option1" "config" { "osarch" "32" } }
        "1" { "executable" "Game.exe" "type" "default" "config" { "osarch" "32" } }
    } })",only32));
    auto only32Options=entries(only32);
    assert(choose(only32Options)==&only32Options[1]);

    // Among equally 64-bit-capable options the "default" type still wins.
    Vdf equal;
    assert(vdf_parse(R"("config" { "launch" {
        "0" { "executable" "Option.exe" "type" "option1" "config" { "osarch" "64" } }
        "1" { "executable" "Default.exe" "type" "default" "config" { "osarch" "64" } }
    } })",equal));
    auto equalOptions=entries(equal);
    assert(choose(equalOptions)==&equalOptions[1]);

    // An option gated on DLC ownership we cannot check is not the main launch.
    Vdf dlc;
    assert(vdf_parse(R"("config" { "launch" {
        "0" { "executable" "Dlc.exe" "type" "default" "config" { "oslist" "windows" "osarch" "64" "ownsdlc" "1234" } }
        "1" { "executable" "Game.exe" "type" "default" "config" { "oslist" "windows" "osarch" "32" } }
    } })",dlc));
    auto dlcOptions=entries(dlc);
    assert(dlcOptions[0].ownsdlc=="1234");
    assert(choose(dlcOptions)==&dlcOptions[1]);
    Vdf noDlc;
    assert(vdf_parse(R"("config" { "launch" {
        "0" { "executable" "Game.exe" "type" "default" "config" { "osarch" "64" "ownsdlc" "0" } }
    } })",noDlc));
    auto noDlcOptions=entries(noDlc);
    assert(choose(noDlcOptions)==&noDlcOptions[0]);

    // Non-Windows and beta-keyed options are never chosen, even when 64-bit.
    Vdf excluded;
    assert(vdf_parse(R"("config" { "launch" {
        "0" { "executable" "Mac.app" "config" { "oslist" "macos,linux" "osarch" "64" } }
        "1" { "executable" "Beta.exe" "type" "default" "config" { "oslist" "windows" "osarch" "64" "betakey" "beta" } }
    } })",excluded));
    auto excludedOptions=entries(excluded);
    assert(!choose(excludedOptions));

    // The picker lists the game's own options, 64-bit first, labelled by
    // description (else the executable); both architectures are startable.
    auto rows=choices(options);
    assert(rows.size()==2);
    assert(rows[0].label=="DirectX" && rows[0].selectable && rows[0].arch==arch64);
    assert(rows[1].label=="32-bit" && rows[1].selectable && rows[1].arch==arch32);
    assert(arch_note(arch32) && !arch_note(arch64) && !arch_note(archUnknown));
    // No description: the executable path is the label. Without a PE probe the
    // architecture is unstated, so the client's type order still decides.
    assert(choices(unstatedOptions)[0].label=="Game32.exe");
    auto probed=choices(unstatedOptions,[&](const std::string& exe){return pe.count(exe)?pe[exe]:archUnknown;});
    assert(probed[0].label=="Game64.exe" && probed[0].selectable && probed[1].selectable && probed[1].arch==arch32);
    // Preselection: nothing remembered -> the first row (64-bit comes first); a
    // remembered option is kept when it is still offered; a missing one falls
    // back to the first row.
    assert(preselect(rows,"")==0);
    assert(preselect(rows,"x86\\Hades.exe")==1);   // a 32-bit option is runnable and remembered
    assert(preselect(rows,"x64\\Hades.exe")==0);
    assert(preselect(rows,"gone.exe")==0);
    assert(preselect({},"")==0);
    Vdf two64;
    assert(vdf_parse(R"("config" { "launch" {
        "0" { "executable" "A.exe" "type" "default" "config" { "osarch" "64" } }
        "1" { "executable" "B.exe" "type" "option1" "config" { "osarch" "64" } }
    } })",two64));
    auto twoRows=choices(entries(two64));
    assert(twoRows.size()==2 && preselect(twoRows,"")==0 && preselect(twoRows,"B.exe")==1);

    // Fallback (no usable launch option): a PE32+ exe wins over a larger PE32
    // one, and size decides among equals.
    std::vector<Candidate> both{{"Big32.exe",5000,arch32},{"Small64.exe",1000,arch64}};
    auto best=largest(both);
    assert(best && best->name=="Small64.exe");
    std::vector<Candidate> sameArch{{"Small.exe",1000,arch64},{"Big.exe",5000,arch64}};
    assert(largest(sameArch)->name=="Big.exe");
    std::vector<Candidate> unknown{{"Big.unknown",5000,archUnknown},{"Small32.exe",10,arch32}};
    assert(largest(unknown)->name=="Big.unknown");
    std::vector<Candidate> unreadable{{"Small64.exe",10,arch64},{"Big.unknown",5000,archUnknown}};
    assert(largest(unreadable)->name=="Small64.exe");
    assert(!largest({}));

    // The chosen option's arguments and working directory reach the launch.
    Vdf work;
    assert(vdf_parse(R"("config" { "launch" {
        "0" { "executable" "bin\\Game.exe" "arguments" "-dx11" "workingdir" "bin" "type" "default" "config" { "osarch" "64" } }
    } })",work));
    auto workOptions=entries(work);
    assert(workOptions[0].workingdir=="bin");
    assert(arguments(choose(workOptions),false)=="-dx11");
    assert(relative_path(workOptions[0].workingdir,rel) && rel=="bin");

    // Unity engine options: D3D11 by default, Vulkan only when asked
    // (PLAY_FORCE_VULKAN), and the game's own arguments always stay first.
    assert(std::string(unity_arguments()).find("-force-d3d11")!=std::string::npos);
    assert(std::string(unity_arguments()).find("-force-vulkan")==std::string::npos);
    assert(std::string(unity_arguments(true)).find("-force-vulkan")!=std::string::npos);
    assert(std::string(unity_arguments(true)).find("-force-d3d11")==std::string::npos);
    assert(arguments(nullptr,false).empty());
    assert(arguments(nullptr,false,true).empty());
    assert(arguments(choose(workOptions),true)=="-dx11 -screen-fullscreen 0 -force-d3d11 -logFile vis-player.log");
    assert(arguments(choose(workOptions),true,true)=="-dx11 -screen-fullscreen 0 -force-vulkan -logFile vis-player.log");
    assert(arguments(nullptr,true)=="-screen-fullscreen 0 -force-d3d11 -logFile vis-player.log");
    // The opt-in's values: only an explicit on-switch asks for Vulkan; the
    // default (no value, "0", a typo) stays D3D11.
    assert(vulkan_flag("1") && vulkan_flag("on") && vulkan_flag("TRUE") && vulkan_flag(" yes\r\n"));
    assert(!vulkan_flag("") && !vulkan_flag("0") && !vulkan_flag("off") && !vulkan_flag("false"));
    assert(!vulkan_flag("vulkan") && !vulkan_flag("2"));
    // A settings file may hold the bare value or a keyed line.
    assert(vulkan_setting("1\n"));
    assert(vulkan_setting("vulkan=1\n"));
    assert(vulkan_setting("force-vulkan = on\n"));
    assert(vulkan_setting("PLAY_FORCE_VULKAN=yes\n"));
    assert(!vulkan_setting("vulkan=0\n"));
    assert(!vulkan_setting("fps=1\n"));
    assert(!vulkan_setting(""));

    std::puts("LAUNCH-TARGET-OK: 64-bit launch option wins; picker lists options 64-bit first, both startable; PE32+ preferred in the fallback; Unity runs D3D11 unless PLAY_FORCE_VULKAN asks for Vulkan");

    // ---- the picker: only a game with more than one runnable option asks.
    using namespace launch_picker;
    assert(needed(rows) && !needed({rows[0]}) && !needed({}));
    State s;
    open(s, rows, "");
    assert(s.open && s.sel == 0 && current(s) && current(s)->label == "DirectX");
    move(s, 1);   // the 32-bit row is reachable and starts in the interpreter
    assert(s.sel == 1 && current(s)->label == "32-bit");
    assert(press_a(s) == Press::start && started(s) == "x86\\Hades.exe");
    move(s, 1);   // wraps back to the 64-bit row
    assert(s.sel == 0 && s.note.empty());
    assert(press_a(s) == Press::start && started(s) == "x64\\Hades.exe");
    move(s, -1);
    assert(s.sel == 1);
    // The remembered option reopens preselected; a stale one does not.
    open(s, rows, "x64\\Hades.exe");
    assert(s.sel == 0);
    open(s, rows, "x86\\Hades.exe");   // a remembered 32-bit option reopens on its own row
    assert(s.sel == 1);
    open(s, rows, "gone.exe");
    assert(s.sel == 0);
    close(s);
    assert(!s.open && s.rows.empty() && s.sel == 0);
    assert(press_a(s) == Press::none);

    // Remembered choice file: one executable line, nothing else accepted.
    assert(remember_name(1145360) == "launch_choice_1145360.txt");
    assert(parse_remember(format_remember("x64\\Hades.exe")) == "x64\\Hades.exe");
    assert(parse_remember("x64\\Hades.exe\r\n") == "x64\\Hades.exe");
    assert(parse_remember("") .empty() && parse_remember("\n").empty() && parse_remember(std::string(300, 'a')).empty());
    assert(parse_remember("x64\\Hades.exe\nsecond") == "x64\\Hades.exe");
    assert(parse_remember(std::string("a\tb")).empty() && parse_remember(std::string("a\0b", 3)).empty());

    // The rows own their entries: they stay valid after the appinfo buffer
    // (and the vector `entries()` parsed it into) is gone, as on the console
    // where the picker outlives the parse.
    std::vector<Choice> detached;
    {
        Vdf live;
        assert(vdf_parse(R"("config" { "launch" {
            "0" { "executable" "x64\\Hades.exe" "arguments" "-dx11" "workingdir" "x64" "type" "default" "description" "DirectX" "config" { "osarch" "64" } }
            "1" { "executable" "x86\\Hades.exe" "type" "option1" "config" { "osarch" "32" } }
        } })",live));
        detached=choices(entries(live));
    }
    assert(detached.size()==2);
    assert(detached[0].entry.executable=="x64\\Hades.exe" && detached[0].entry.arguments=="-dx11" && detached[0].entry.workingdir=="x64");
    assert(preselect(detached,"x64\\Hades.exe")==0 && detached[1].entry.executable=="x86\\Hades.exe");

    std::puts("LAUNCH-PICKER-OK: several options ask; a 32-bit row starts in the interpreter; last choice remembered");
}
