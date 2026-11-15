// SPDX-License-Identifier: GPL-3.0-or-later
#include "../steam_genuine.h"
#include "../steam_token_store.h"
#include <cstdio>
#include <cstring>
static int checks,failures;
#define CHECK(x) do { ++checks; if(!(x)){++failures;std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);} } while(0)
int main() {
    CHECK(steam_genuine::opt_in("1"));
    CHECK(steam_genuine::opt_in(" 1\r\n"));
    CHECK(!steam_genuine::opt_in(""));
    CHECK(!steam_genuine::opt_in("0"));
    CHECK(!steam_genuine::opt_in("1 extra"));
    char users[2048],autologin[64],config[32768];
    CHECK(steam_genuine::loginusers_vdf("1234567890",users,sizeof users));
    CHECK(std::strstr(users,"\"AccountName\"\t\t\"1234567890\"")!=nullptr);
    CHECK(std::strstr(users,"\"RememberPassword\"\t\t\"1\"")!=nullptr);
    CHECK(std::strstr(users,"\"AllowAutoLogin\"\t\t\"1\"")!=nullptr);
    CHECK(std::strstr(users,"\"MostRecent\"\t\t\"1\"")!=nullptr);
    CHECK(!steam_genuine::loginusers_vdf("bad-id",users,sizeof users));
    CHECK(steam_genuine::autologin_user("1234567890",autologin,sizeof autologin));
    CHECK(std::strcmp(autologin,"1234567890")==0);
    CHECK(steam_token_store_config_vdf("fake-refresh-token","1234567890","1234567890",1500000000ULL,config,sizeof config)>0);
    CHECK(std::strstr(config,"\"ConnectCache\"")!=nullptr);
    CHECK(std::strstr(config,"fake-refresh-token")==nullptr);
    std::printf("STEAM GENUINE checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}
