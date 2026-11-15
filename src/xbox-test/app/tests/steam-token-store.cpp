// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic coverage for SteamTokenLogin.kt's ConnectCache writer.
#include "../steam_token_store.h"
#include <cstdio>
#include <cstring>

static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::puts("FAIL: " #x); } } while (0)

int main() {
    const char token[]="synthetic-refresh-token";
    char encoded[128], repeated[128], other[128], key[16], config[32768];
    CHECK(steam_token_store_obfuscate(token,sizeof(token)-1,1500000000ULL,encoded,sizeof encoded)>0);
    CHECK(std::strncmp(encoded,"02000000",8)==0);
    CHECK(steam_token_store_obfuscate(token,sizeof(token)-1,1500000000ULL,repeated,sizeof repeated)>0);
    CHECK(std::strcmp(encoded,repeated)==0);
    CHECK(steam_token_store_obfuscate(token,sizeof(token)-1,1500000001ULL,other,sizeof other)>0);
    CHECK(std::strcmp(encoded,other)!=0);
    CHECK(steam_token_store_cache_key("test-user",key,sizeof key)>0);
    CHECK(std::strcmp(key,"bf84f291")==0);
    const size_t config_size=steam_token_store_config_vdf(token,"test-user","12345678901234567",1500000000ULL,config,sizeof config);
    CHECK(config_size>0);
    CHECK(std::strstr(config,"\"MTBF\"\t\t\"1500000000\"")!=nullptr);
    CHECK(std::strstr(config,"\"ConnectCache\"")!=nullptr);
    CHECK(std::strstr(config,"\"bf84f291\"")!=nullptr);
    CHECK(std::strstr(config,"\"SteamID\"\t\t\"12345678901234567\"")!=nullptr);
    CHECK(std::strstr(config,token)==nullptr);
    CHECK(steam_token_store_config_vdf(token,"bad\"login","12345678901234567",1500000000ULL,config,sizeof config)==0);
    CHECK(steam_token_store_config_vdf(token,"test-user","12345678901234567",2000000000ULL,config,sizeof config)==0);
    std::printf("STEAM TOKEN STORE checks=%d failures=%d\n",checks,failures);
    return failures ? 1 : 0;
}
