// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include "../host/x86/x86_play.h"
using namespace x86;
static std::vector<uint8_t> slurp(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::printf("FAIL cannot open %s\n", path); std::exit(2); }
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char** argv) {
    if (argc != 3) { std::puts("usage: x86-steam-provider <fixture.exe> <steam_api.dll>"); return 2; }
    int owner_calls=0, stage_calls=0;
    if (stage_owned(false,[&]{++owner_calls;return true;},[&]{++stage_calls;return true;}) || owner_calls || stage_calls) {
        std::puts("FAIL unsigned account reached ownership/stage callback"); return 1;
    }
    if (stage_owned(true,[&]{++owner_calls;return false;},[&]{++stage_calls;return true;}) || owner_calls!=1 || stage_calls) {
        std::puts("FAIL ownership failure staged emulator"); return 1;
    }
    if (!stage_owned(true,[&]{++owner_calls;return true;},[&]{++stage_calls;return true;}) || owner_calls!=2 || stage_calls!=1) {
        std::puts("FAIL owned account did not stage emulator"); return 1;
    }
    auto provider=slurp(argv[2]);
    DllReader staged=[provider](const std::string& name) { return name=="steam_api.dll"||name=="steamclient.dll"?provider:std::vector<uint8_t>{}; };
    DllReader game=[](const std::string&){return std::vector<uint8_t>{};};
    DllSource noSteam=make_steam_dll_source(game,staged,nullptr,false,false);
    if (!noSteam("steam_api.dll").empty() || !noSteam("steamclient.dll").empty()) {
        std::puts("FAIL denied ownership exposed a staged Steam DLL"); return 1;
    }
    DllSource owned=make_steam_dll_source(game,staged,nullptr,true,false);
    if (owned("steam_api.dll")!=provider) { std::puts("FAIL owned x86 provider was not served"); return 1; }
    DllReader original=[](const std::string& name){return name=="steam_api.dll"?std::vector<uint8_t>{1,2,3}:std::vector<uint8_t>{};};
    DllSource wrapped=make_steam_dll_source(original,staged,nullptr,true,true);
    if (wrapped("steam_api.dll")!=std::vector<uint8_t>({1,2,3}) || wrapped("steamclient.dll")!=provider) {
        std::puts("FAIL SteamStub did not keep its original API and serve the staged x86 client"); return 1;
    }
    // Staged module paths: names are case-insensitive, an empty path removes one, clear drops all.
    set_staged_module_path("Steam_Api.DLL","C:\\a.dll");
    if (staged_module_paths().size()!=1 || staged_module_paths().count("steam_api.dll")!=1) { std::puts("FAIL staged module path not keyed by lowercase name"); return 1; }
    set_staged_module_path("steam_api.dll","");
    if (!staged_module_paths().empty()) { std::puts("FAIL empty path did not remove the staged module"); return 1; }
    set_staged_module_path("steam_api.dll","C:\\a.dll"); clear_staged_module_paths();
    if (!staged_module_paths().empty()) { std::puts("FAIL clear left staged module paths"); return 1; }
    auto file=slurp(argv[1]);
    // The fixture picks its scenario from its command line's last character and
    // reports a distinct exit code per scenario; see x86-steam-provider-fixture.c.
    const char* stagedApi="C:\\staged\\steam\\480\\steam_api.dll";
    const char* stagedClient="C:\\staged\\steam\\480\\steamclient.dll";
    auto run=[&](const char* cmdline,const DllSource& source) { return run_image(file,"fixture.exe",cmdline,nullptr,nullptr,500000000ull,source); };
    auto check=[](const char* scenario,const PlayResult& result,uint32_t want) {
        if (result.finished && result.exit_code==want && result.unresolved.empty() && result.fault.empty()) return true;
        std::printf("FAIL %s: loaded=%d finished=%d exit=%u want=%u unresolved=%zu fault=%s\n",scenario,
                    result.loaded?1:0,result.finished?1:0,result.exit_code,want,result.unresolved.size(),result.fault.c_str());
        for (const auto& name:result.unresolved) std::printf("unresolved %s\n",name.c_str());
        return false;
    };
    // Owned standard provider: the staged steam_api.dll reports its staged path
    // (GetModuleFileNameA/W by handle, and from inside the DLL as gbe does), the
    // main module keeps the exe name, ActiveProcess answers owner and client.
    set_steam_client_registry(1234,"C:\\steamclient.dll");
    set_staged_module_path("steam_api.dll",stagedApi);
    bool ok=check("owned standard provider",run("fixture.exe s",owned),42);
    clear_staged_module_paths();
    // Nothing staged (no ownership): the game's own steam_api.dll is served, no
    // staged path is reported for it and ActiveProcess is not published.
    set_steam_client_registry(0,"");
    DllReader gameApi=[provider](const std::string& name){return name=="steam_api.dll"?provider:std::vector<uint8_t>{};};
    ok=check("nothing staged",run("fixture.exe n",make_steam_dll_source(gameApi,staged,nullptr,false,false)),43)&&ok;
    // SteamStub: the game keeps its steam_api.dll; the client ActiveProcess names
    // is the staged steamclient.dll and loading it reports exactly that path.
    set_steam_client_registry(1234,stagedClient);
    set_staged_module_path("steamclient.dll",stagedClient);
    ok=check("SteamStub staged client",run("fixture.exe w",make_steam_dll_source(gameApi,staged,nullptr,true,true)),44)&&ok;
    set_steam_client_registry(0,"");
    clear_staged_module_paths();
    if (!ok) return 1;
    std::puts("X86-STEAM-PROVIDER PASS: ownership gate blocks unsigned/unowned staging, PE32 SteamAPI_Init binds to the staged provider, SteamStub keeps steam_api.dll and serves steamclient.dll, ActiveProcess reads owner/client values, GetModuleFileNameA/W report staged DLL paths and keep the exe name for the main module");
    return 0;
}
