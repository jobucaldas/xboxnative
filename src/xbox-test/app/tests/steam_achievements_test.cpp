// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression: schema decoding, gbe definition/seed files and the
// ClientStoreUserStats2 request, against a synthetic schema and fake service.
#include "../steam_achievements.h"
#include <cassert>
using namespace steam_achievements;
static void kv_str(std::string& o,const std::string& k,const std::string& v){o+='\x01';o+=k;o+='\0';o+=v;o+='\0';}
static void kv_int(std::string& o,const std::string& k,int32_t v){o+='\x02';o+=k;o+='\0';o.append((const char*)&v,4);}
static void kv_open(std::string& o,const std::string& k){o+='\x00';o+=k;o+='\0';}
static void kv_close(std::string& o){o+='\x08';}
static std::string schema() {
    std::string s;kv_open(s,"480");kv_open(s,"stats");
    kv_open(s,"1");kv_str(s,"type","ACHIEVEMENTS");kv_open(s,"bits");
    for(int b=0;b<3;++b){kv_open(s,std::to_string(b));kv_str(s,"name","ACH_"+std::to_string(b));
        kv_open(s,"display");kv_open(s,"name");kv_str(s,"english","Win \"" + std::to_string(b)+"\"");kv_close(s);
        kv_open(s,"desc");kv_str(s,"english","Do it");kv_close(s);kv_int(s,"hidden",b==2);kv_str(s,"icon","i.jpg");kv_close(s);kv_close(s);}
    kv_close(s);kv_close(s);
    kv_open(s,"2");kv_str(s,"type","INT");kv_str(s,"name","kills");kv_close(s);
    kv_open(s,"3");kv_str(s,"type","FLOAT");kv_str(s,"name","distance");kv_close(s);
    kv_close(s);kv_str(s,"version","3");kv_close(s);kv_close(s);
    return s;
}
int main() {
    auto parsed=parse_schema(schema());
    assert(parsed&&parsed->achievements.size()==3&&parsed->stats.size()==2);
    assert(parsed->achievements[2].hidden&&parsed->achievements[0].display=="Win \"0\""&&parsed->achievements[1].bit==1);
    assert(parsed->stats[0].type=="int"&&parsed->stats[1].type=="float"&&parsed->stats[1].id==3);
    assert(!parse_schema("garbage"));

    // Fake GetUserStats response: ACH_0 unlocked at 1000, kills=5, crc 77.
    std::string resp;pb_u(resp,2,1);pb_u(resp,3,77);pb_s(resp,4,schema());
    {std::string st;pb_u(st,1,2);pb_u(st,2,5);pb_s(resp,5,st);}
    {std::string blk;pb_u(blk,1,1);for(uint32_t t:{1000u,0u,0u}){blk+=char(0x15);blk.append((const char*)&t,4);}pb_s(resp,6,blk);}
    std::string lastStore;unsigned stores=0;
    Io io;io.log=[](const std::string&){};
    io.call=[&](uint32_t emsg,const std::string& body,std::string& out){
        if(emsg==ClientGetUserStats){auto f=pb_parse(body);assert(pb_get(f,1)->u==480&&pb_get(f,4)->u==7);out=resp;return true;}
        if(emsg==ClientStoreUserStats2){lastStore=body;++stores;out.clear();pb_u(out,2,1);return true;}
        return false;};
    auto u=fetch(io,480,7);
    assert(u.ok&&u.crc==77&&u.values[2]==5&&u.unlocks[1][0]==1000&&u.unlocks[1][1]==0);

    auto root=fs::temp_directory_path()/"steam-ach-test";fs::remove_all(root);
    auto settings=root/"steam_settings",saves=root/"saves"/"480";
    write(saves/"achievements.json","{\"ACH_2\": {\"earned\": true, \"earned_time\": 50}}");   // earned offline earlier
    write(saves/"stats"/"kills",std::string("\x09\0\0\0",4));                                 // local progress 9 > 5
    assert(prepare(*parsed,u,settings,saves));
    auto defs=*read(settings/"achievements.json");
    assert(defs.find("\"name\":\"ACH_1\"")!=std::string::npos&&defs.find("Win \\\"0\\\"")!=std::string::npos&&defs.find("\"hidden\":\"1\"")!=std::string::npos);
    assert(read(settings/"stats.json")->find("\"type\":\"float\"")!=std::string::npos);
    auto earned=parse_earned(*read(saves/"achievements.json"));
    assert(earned["ACH_0"].earned&&earned["ACH_0"].time==1000&&earned["ACH_2"].earned&&earned["ACH_2"].time==50&&!earned.count("ACH_1"));
    assert(*read(saves/"stats"/"kills")==std::string("\x09\0\0\0",4));   // larger local progress kept

    // gbe then earns ACH_1 and kills reaches 12: store ORs bits 0|1|2 and sends kills.
    write(saves/"achievements.json","{\"ACH_0\":{\"earned\":true,\"earned_time\":1000},\"ACH_1\":{\"earned\":true,\"earned_time\":2000},\"ACH_2\":{\"earned\":true,\"earned_time\":50},\"x\":[1,{\"a\":2}]}");
    write(saves/"stats"/"kills",std::string("\x0c\0\0\0",4));
    auto up=store(io,480,7,*parsed,u,saves);
    assert(up.sent&&up.ok&&up.achievements==2&&up.stats==1&&stores==1);
    auto f=pb_parse(lastStore);
    assert(pb_get(f,1)->u==480&&pb_get(f,2)->u==7&&pb_get(f,3)->u==7&&pb_get(f,4)->u==77);
    std::map<uint32_t,uint32_t> sent;for(auto& x:f)if(x.f==6){auto s=pb_parse(x.s);sent[uint32_t(pb_get(s,1)->u)]=uint32_t(pb_get(s,2)->u);}
    assert(sent.size()==2&&sent[1]==0x7&&sent[2]==12);
    // Server already has it all: nothing is sent; progress never moves backwards.
    UserStats synced=u;synced.unlocks[1]={1000,2000,50};synced.values[2]=12;
    auto none=store(io,480,7,*parsed,synced,saves);assert(!none.sent&&none.ok&&stores==1);
    synced.values[2]=40;auto back=store(io,480,7,*parsed,synced,saves);assert(!back.sent&&stores==1);

    // item D6: the game page's achievements row counts the prepared files.
    assert(count_text(*parsed,earned)=="2 / 3");   // ACH_0 (server) + ACH_2 (earned offline)
    assert(count_text(*parsed,std::map<std::string,Earned>{})==std::string("0 / 3"));
    assert(count_text(Schema{},earned).empty());   // no schema: the page says "Not tracked yet"
    assert(count_from_files(settings/"achievements.json",saves/"achievements.json")=="3 / 3");
    write(saves/"achievements.json","{\"ACH_0\":{\"earned\":true,\"earned_time\":1000}}");
    assert(count_from_files(settings/"achievements.json",saves/"achievements.json")=="1 / 3");
    assert(count_from_files(root/"nope.json",saves/"achievements.json").empty());   // never played here
    write(settings/"garbled.json","not json");
    assert(count_from_files(settings/"garbled.json",saves/"achievements.json").empty());
    fs::remove_all(root);
    puts("STEAM-ACHIEVEMENTS PASS: schema, gbe definitions, seeded unlocks/stats, OR-only store request");
}
