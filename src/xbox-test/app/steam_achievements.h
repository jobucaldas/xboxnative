// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Steam achievements and stats for owned games served by the Steam API provider
// (gbe_fork; the ownership rule), synced with the player's real Steam account over
// the launcher's session. Portable: network and logging are injected.
//
// Before launch: ClientGetUserStats (schema + the player's values) becomes
// gbe_fork's steam_settings/achievements.json and stats.json, and the player's
// unlocks and stat values are merged into gbe's saves (<saves>/<appid>/
// achievements.json, stats/<name>). After play: gbe's earned achievements are
// OR-ed into the server's achievement bitmasks (never cleared) and changed stat
// values are sent in one ClientStoreUserStats2 with the server's schema CRC.
#include "steam_wire.h"
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>

namespace steam_achievements {
namespace fs = std::filesystem;
enum : uint32_t { ClientGetUserStats=818, ClientGetUserStatsResponse=819,
                  ClientStoreUserStatsResponse=821, ClientStoreUserStats2=5466 };
struct Io {
    // EMsg job call; returns false on transport failure.
    std::function<bool(uint32_t emsg,const std::string& body,std::string& response)> call;
    std::function<void(const std::string&)> log;
};
// ---- binary KeyValues (the schema blob)
struct Kv { std::string value; std::vector<std::pair<std::string,Kv>> kids; bool tree=false;
    const Kv* get(const std::string& k) const { for(auto& c:kids) if(c.first==k) return &c.second; return nullptr; }
    std::string str(const std::string& k) const { auto c=get(k); return c&&!c->tree?c->value:""; } };
inline bool kv_parse(const std::string& d,size_t& i,Kv& out,int depth=0) {
    if(depth>32) return false;
    out.tree=true;
    for(;;) {
        if(i>=d.size()) return false;
        uint8_t t=uint8_t(d[i++]);
        if(t==8||t==11) return true;
        size_t end=d.find('\0',i);if(end==std::string::npos) return false;
        std::string key=d.substr(i,end-i);i=end+1;
        Kv v;
        if(t==0){if(!kv_parse(d,i,v,depth+1)) return false;}
        else if(t==1){end=d.find('\0',i);if(end==std::string::npos) return false;v.value=d.substr(i,end-i);i=end+1;}
        else if(t==2||t==3||t==4||t==6){if(i+4>d.size()) return false;
            if(t==3){float f;memcpy(&f,&d[i],4);char b[32];snprintf(b,sizeof b,"%g",double(f));v.value=b;}
            else{int32_t n;memcpy(&n,&d[i],4);v.value=std::to_string(n);} i+=4;}
        else if(t==7||t==10){if(i+8>d.size()) return false;uint64_t n;memcpy(&n,&d[i],8);v.value=std::to_string(n);i+=8;}
        else return false;
        out.kids.emplace_back(key,std::move(v));
    }
}
struct Achievement { uint32_t block=0, bit=0; std::string name, display, description, icon, iconGray; bool hidden=false; };
struct Stat { uint32_t id=0; std::string name, type; };   // type: int, float, avgrate
struct Schema { std::vector<Achievement> achievements; std::vector<Stat> stats; };
inline std::string english(const Kv* k) { if(!k) return ""; if(!k->tree) return k->value; auto e=k->str("english"); return e; }
inline std::optional<Schema> parse_schema(const std::string& blob) {
    Kv root;size_t i=0;if(!kv_parse(blob+std::string(1,'\x08'),i,root)||root.kids.empty()) return std::nullopt;
    auto stats=root.kids[0].second.get("stats");if(!stats) return std::nullopt;
    Schema s;
    for(auto& [id,stat]:stats->kids) {
        uint32_t block=uint32_t(strtoul(id.c_str(),nullptr,10));
        auto type=stat.str("type");
        if(type=="ACHIEVEMENTS"||type=="4") {
            if(auto bits=stat.get("bits")) for(auto& [bitKey,bit]:bits->kids) {
                Achievement a;a.block=block;a.bit=uint32_t(strtoul(bitKey.c_str(),nullptr,10));if(a.bit>31) continue;
                a.name=bit.str("name");auto display=bit.get("display");
                if(display){a.display=english(display->get("name"));a.description=english(display->get("desc"));
                    a.hidden=display->str("hidden")=="1";a.icon=display->str("icon");a.iconGray=display->str("icon_gray");}
                if(!a.name.empty()) s.achievements.push_back(a);
            }
        } else if(type=="INT"||type=="1"||type=="FLOAT"||type=="2"||type=="AVGRATE"||type=="3") {
            auto name=stat.str("name");if(name.empty()) continue;
            s.stats.push_back({block,name,type=="FLOAT"||type=="2"?"float":type=="AVGRATE"||type=="3"?"avgrate":"int"});
        }
    }
    return s;
}
struct UserStats { bool ok=false; uint32_t crc=0; std::string schema; std::map<uint32_t,uint32_t> values; std::map<uint32_t,std::array<uint32_t,32>> unlocks; };
inline UserStats fetch(Io& io,uint32_t appid,uint64_t steamid) {
    UserStats u;std::string req;pb_fixed64(req,1,appid);pb_u(req,3,uint64_t(-1));pb_fixed64(req,4,steamid);
    std::string resp;if(!io.call(ClientGetUserStats,req,resp)){io.log("achievements: stats request failed");return u;}
    int eresult=2;
    for(auto& f:pb_parse(resp)) {
        if(f.f==2) eresult=int(f.u); else if(f.f==3) u.crc=uint32_t(f.u); else if(f.f==4) u.schema=f.s;
        else if(f.f==5){uint32_t id=0,v=0;for(auto& x:pb_parse(f.s)){if(x.f==1)id=uint32_t(x.u);else if(x.f==2)v=uint32_t(x.u);}u.values[id]=v;}
        else if(f.f==6){uint32_t id=0;std::array<uint32_t,32> times{};unsigned n=0;
            for(auto& x:pb_parse(f.s)){if(x.f==1)id=uint32_t(x.u);
                else if(x.f==2){if(x.wt==5){if(n<32)times[n++]=uint32_t(x.u);}
                    else if(x.wt==2)for(size_t k=0;k+4<=x.s.size()&&n<32;k+=4){uint32_t t;memcpy(&t,&x.s[k],4);times[n++]=t;}}}
            u.unlocks[id]=times;}
    }
    u.ok=eresult==1&&!u.schema.empty();
    if(!u.ok) io.log("achievements: stats unavailable eresult="+std::to_string(eresult));
    return u;
}
// ---- minimal JSON (gbe's saved achievements object) and writers
inline std::string json_escape(const std::string& s) {
    std::string o;for(unsigned char c:s){if(c=='"'||c=='\\'){o+='\\';o+=char(c);}else if(c<0x20){char b[8];snprintf(b,sizeof b,"\\u%04x",c);o+=b;}else o+=char(c);}return o;
}
struct Earned { bool earned=false; uint32_t time=0; };
// Parses {"NAME":{"earned":true,"earned_time":123,...},...}; other values skipped.
struct JsonReader {
    const std::string& j; size_t i=0;
    void ws() { while(i<j.size()&&isspace(uint8_t(j[i]))) ++i; }
    bool at(char c) { ws(); return i<j.size()&&j[i]==c; }
    bool eat(char c) { if(!at(c)) return false; ++i; return true; }
    bool string(std::string& out) {
        if(!eat('"')) return false;
        out.clear();
        while(i<j.size()&&j[i]!='"') {
            if(j[i]=='\\'&&i+1<j.size()) ++i;
            out+=j[i++];
        }
        return eat('"');
    }
    // Skips any value; returns its raw text for scalars.
    bool value(std::string& raw,int depth=0) {
        raw.clear();
        if(depth>32) return false;
        ws();
        if(i>=j.size()) return false;
        if(j[i]=='"') return string(raw);
        if(j[i]=='{'||j[i]=='[') {
            const bool object=j[i]=='{';
            const char close=object?'}':']';
            ++i;
            if(eat(close)) return true;
            for(;;) {
                std::string ignored;
                if(object&&(!string(ignored)||!eat(':'))) return false;
                if(!value(ignored,depth+1)) return false;
                if(eat(',')) continue;
                return eat(close);
            }
        }
        size_t start=i;
        while(i<j.size()&&!strchr(",}] \t\r\n",j[i])) ++i;
        raw=j.substr(start,i-start);
        return !raw.empty();
    }
};
inline std::map<std::string,Earned> parse_earned(const std::string& text) {
    std::map<std::string,Earned> out;
    JsonReader r{text};
    if(!r.eat('{')||r.eat('}')) return out;
    for(;;) {
        std::string name;
        if(!r.string(name)||!r.eat(':')) return out;
        Earned e;
        if(r.eat('{')) {
            if(!r.eat('}')) for(;;) {
                std::string key,raw;
                if(!r.string(key)||!r.eat(':')||!r.value(raw)) return out;
                if(key=="earned") e.earned=raw=="true";
                else if(key=="earned_time") e.time=uint32_t(strtoul(raw.c_str(),nullptr,10));
                if(r.eat(',')) continue;
                if(!r.eat('}')) return out;
                break;
            }
        } else {
            std::string raw;
            if(!r.value(raw)) return out;
        }
        out[name]=e;
        if(!r.eat(',')) return out;
    }
}
inline std::optional<std::string> read(const fs::path& p){std::ifstream in(p,std::ios::binary);if(!in)return std::nullopt;return std::string((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());}
inline bool write(const fs::path& p,const std::string& d){std::error_code ec;fs::create_directories(p.parent_path(),ec);std::ofstream o(p,std::ios::binary|std::ios::trunc);o.write(d.data(),std::streamsize(d.size()));return bool(o);}
// Writes gbe's definitions and merges the player's server state into its saves.
inline bool prepare(const Schema& s,const UserStats& u,const fs::path& settings,const fs::path& saves) {
    std::string a="[\n";
    for(size_t k=0;k<s.achievements.size();++k){auto& x=s.achievements[k];
        a+="  {\"name\":\""+json_escape(x.name)+"\",\"displayName\":\""+json_escape(x.display)+"\",\"description\":\""+json_escape(x.description)+
           "\",\"hidden\":\""+(x.hidden?"1":"0")+"\",\"icon\":\""+json_escape(x.icon)+"\",\"icongray\":\""+json_escape(x.iconGray)+"\"}"+(k+1<s.achievements.size()?",":"")+"\n";}
    a+="]\n";
    std::string st="[\n";
    for(size_t k=0;k<s.stats.size();++k){auto& x=s.stats[k];
        st+="  {\"name\":\""+json_escape(x.name)+"\",\"type\":\""+x.type+"\",\"default\":\"0\",\"global\":\"0\"}"+(k+1<s.stats.size()?",":"")+"\n";}
    st+="]\n";
    if(!write(settings/"achievements.json",a)||!write(settings/"stats.json",st)) return false;
    auto earned=parse_earned(read(saves/"achievements.json").value_or("{}"));
    for(auto& x:s.achievements){auto it=u.unlocks.find(x.block);if(it==u.unlocks.end()||!it->second[x.bit])continue;
        auto& e=earned[x.name];if(!e.earned){e.earned=true;e.time=it->second[x.bit];}}
    std::string out="{\n";bool first=true;
    for(auto& [name,e]:earned){out+=std::string(first?"":",\n")+"  \""+json_escape(name)+"\": {\"earned\": "+(e.earned?"true":"false")+", \"earned_time\": "+std::to_string(e.time)+"}";first=false;}
    out+="\n}\n";
    if(!write(saves/"achievements.json",out)) return false;
    for(auto& x:s.stats){auto it=u.values.find(x.id);if(it==u.values.end())continue;
        auto path=saves/"stats"/x.name;auto local=read(path);
        if(x.type=="int"&&local&&local->size()==4){int32_t mine;memcpy(&mine,local->data(),4);if(mine>=int32_t(it->second))continue;} // keep larger local progress
        else if(local&&local->size()>=4&&x.type!="int")continue;
        std::string bytes(4,'\0');memcpy(&bytes[0],&it->second,4);if(!write(path,bytes))return false;}
    return true;
}
// Item D6: the game page's "Achievements" row — "unlocked / total" when the
// schema and the player's earned set are known, empty when they are not (the
// page then says "Not tracked yet"). Pure so tests can pin the text.
inline std::string count_text(const Schema& s, const std::map<std::string,Earned>& earned) {
    if(s.achievements.empty()) return std::string();
    unsigned unlocked=0;
    for(auto& a:s.achievements){auto it=earned.find(a.name);if(it!=earned.end()&&it->second.earned)++unlocked;}
    return std::to_string(unlocked)+" / "+std::to_string(s.achievements.size());
}
// The achievement NAMES of gbe's settings file (prepare() writes a JSON array
// of {"name":...,"displayName":...} objects), so the page can count without a
// live CM session. Returns an empty list for a missing/garbled file.
inline std::vector<std::string> parse_names(const std::string& text) {
    std::vector<std::string> out;
    JsonReader r{text};
    if(!r.eat('[')||r.eat(']')) return out;
    for(;;) {
        std::string name;
        if(!r.eat('{')) return out;
        if(!r.eat('}')) for(;;) {
            std::string key,raw;
            if(!r.string(key)||!r.eat(':')||!r.value(raw)) return out;
            if(key=="name") name=raw;
            if(r.eat(',')) continue;
            if(!r.eat('}')) return out;
            break;
        }
        if(!name.empty()) out.push_back(name);
        if(!r.eat(',')) return out;
    }
}
// "unlocked / total" from the two files prepare() leaves behind, or empty when
// the game has no prepared schema yet (never played on this console).
inline std::string count_from_files(const fs::path& settings_json, const fs::path& earned_json) {
    auto names=read(settings_json);
    if(!names) return std::string();
    auto list=parse_names(*names);
    if(list.empty()) return std::string();
    auto earned=parse_earned(read(earned_json).value_or("{}"));
    unsigned unlocked=0;
    for(auto& n:list){auto it=earned.find(n);if(it!=earned.end()&&it->second.earned)++unlocked;}
    return std::to_string(unlocked)+" / "+std::to_string(list.size());
}
struct Upload { unsigned achievements=0, stats=0; bool sent=false, ok=false; };// Sends newly earned achievements and changed stats. Never clears a bit.
inline Upload store(Io& io,uint32_t appid,uint64_t steamid,const Schema& s,const UserStats& u,const fs::path& saves) {
    Upload r;
    auto earned=parse_earned(read(saves/"achievements.json").value_or("{}"));
    std::map<uint32_t,uint32_t> changes;
    for(auto& x:s.achievements){auto e=earned.find(x.name);if(e==earned.end()||!e->second.earned)continue;
        uint32_t current=changes.count(x.block)?changes[x.block]:0;
        if(!changes.count(x.block)){uint32_t mask=0;auto it=u.unlocks.find(x.block);if(it!=u.unlocks.end())for(unsigned b=0;b<32;++b)if(it->second[b])mask|=1u<<b;current=mask;}
        if(current&(1u<<x.bit)){changes[x.block]=current;continue;}
        changes[x.block]=current|(1u<<x.bit);++r.achievements;}
    for(auto it=changes.begin();it!=changes.end();){ // drop blocks with nothing new
        uint32_t mask=0;auto server=u.unlocks.find(it->first);if(server!=u.unlocks.end())for(unsigned b=0;b<32;++b)if(server->second[b])mask|=1u<<b;
        if(it->second==mask)it=changes.erase(it);else ++it;}
    for(auto& x:s.stats){auto local=read(saves/"stats"/x.name);if(!local||local->size()!=4)continue;
        uint32_t v;memcpy(&v,local->data(),4);auto server=u.values.find(x.id);
        if(server!=u.values.end()&&server->second==v)continue;
        if(x.type=="int"&&server!=u.values.end()&&int32_t(v)<int32_t(server->second))continue; // never move progress backwards
        changes[x.id]=v;++r.stats;}
    if(changes.empty()){r.ok=true;return r;}
    std::string req;pb_fixed64(req,1,appid);pb_fixed64(req,2,steamid);pb_fixed64(req,3,steamid);pb_u(req,4,u.crc);pb_u(req,5,0);
    for(auto& [id,v]:changes){std::string st;pb_u(st,1,id);pb_u(st,2,v);pb_s(req,6,st);}
    std::string resp;r.sent=true;
    if(!io.call(ClientStoreUserStats2,req,resp)){io.log("achievements: store failed (transport)");return r;}
    int eresult=2;unsigned failed=0;
    for(auto& f:pb_parse(resp)){if(f.f==2)eresult=int(f.u);else if(f.f==4)++failed;}
    r.ok=eresult==1&&!failed;
    io.log("achievements: store eresult="+std::to_string(eresult)+" achievements="+std::to_string(r.achievements)+
           " stats="+std::to_string(r.stats)+" rejected="+std::to_string(failed));
    return r;
}
}
