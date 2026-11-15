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
    set_steam_client_registry(1234,"C:\\steamclient.dll");
    auto file=slurp(argv[1]);
    auto result=run_image(file,"fixture.exe","fixture.exe",nullptr,nullptr,500000000ull,owned);
    set_steam_client_registry(0,"");
    if (!result.finished || result.exit_code!=42 || !result.unresolved.empty() || !result.fault.empty()) {
        std::printf("FAIL fixture loaded=%d finished=%d exit=%u unresolved=%zu fault=%s\n",
                    result.loaded?1:0,result.finished?1:0,result.exit_code,result.unresolved.size(),result.fault.c_str());
        for (const auto& name:result.unresolved) std::printf("unresolved %s\n",name.c_str());
        return 1;
    }
    std::puts("X86-STEAM-PROVIDER PASS: ownership gate blocks unsigned/unowned staging, PE32 SteamAPI_Init binds to the staged provider, SteamStub keeps steam_api.dll and serves steamclient.dll, ActiveProcess reads owner/client values");
    return 0;
}
