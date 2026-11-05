// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Steam Auto-Cloud sync for owned games run through the Steam API provider
// (AGENTS.md rule 3): the player's real Steam Cloud files are downloaded before
// launch and changed files uploaded after exit, over the launcher's own Steam
// session. Portable: all network, hashing and logging is injected, so the
// planner and wire format are tested natively. Message shapes follow
// steammessages_cloud.steamclient.proto; the flow mirrors the Steam client's
// Auto-Cloud (and GameNative's SteamAutoCloud.kt, GPL-3.0).
//
// Conservative policy, never losing either side: a remote file is downloaded
// only if missing locally or strictly newer than the local copy (a local copy
// being replaced is kept as <name>.xboxnative-backup); a local file is uploaded
// only if absent remotely or changed and not older than the remote copy.
// Deletions are never propagated. Paths never leave the configured roots.
#include "steam_wire.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>

namespace steam_cloud {
namespace fs = std::filesystem;
using Headers = std::vector<std::pair<std::string,std::string>>;
struct Io {
    // Unified service call ("Cloud.X#1"): returns false on transport failure.
    std::function<bool(const std::string& method,const std::string& request,int& eresult,std::string& response)> service;
    std::function<int(bool https,const std::string& host,const std::string& path,const Headers&,std::string& body)> get;
    std::function<int(bool https,const std::string& host,const std::string& path,const Headers&,const std::string& body)> put;
    std::function<std::string(const std::string& data)> sha1;   // 20 raw bytes
    std::function<void(const std::string&)> log;
};
struct Rule { std::string root, path, pattern; bool recursive=false; };
// appinfo "ufs" -> "savefiles" rules (Windows roots only).
inline std::vector<Rule> rules(const Vdf& appinfo) {
    std::vector<Rule> out;
    const Vdf* files=appinfo.at({"ufs","savefiles"});
    if(!files||!files->obj) return out;
    for(auto& rule:files->kids) {
        Rule r{rule.str({"root"}),rule.str({"path"}),rule.str({"pattern"},"*"),rule.str({"recursive"})=="1"};
        if(r.root.rfind("Win",0)==0||r.root=="GameInstall") out.push_back(r);
    }
    return out;
}
// Local directories for UFS root names (e.g. "WinAppDataLocalLow").
using Roots = std::map<std::string,fs::path>;
inline bool glob(const std::string& pattern,const std::string& name) {
    size_t p=0,n=0,star=std::string::npos,mark=0;
    while(n<name.size()) {
        if(p<pattern.size()&&(pattern[p]=='?'||pattern[p]==name[n])){++p;++n;}
        else if(p<pattern.size()&&pattern[p]=='*'){star=p++;mark=n;}
        else if(star!=std::string::npos){p=star+1;n=++mark;}
        else return false;
    }
    while(p<pattern.size()&&pattern[p]=='*')++p;
    return p==pattern.size();
}
// Relative cloud paths use '/', never escape upward and never name a drive.
inline bool safe_relative(const std::string& path) {
    if(path.empty()||path[0]=='/'||path[0]=='\\'||path.find(':')!=std::string::npos) return false;
    size_t start=0;
    while(start<=path.size()) {
        size_t end=path.find_first_of("/\\",start);if(end==std::string::npos)end=path.size();
        auto part=path.substr(start,end-start);
        if(part.empty()||part=="."||part=="..") return false;
        start=end+1;
    }
    return true;
}
// "%WinAppDataLocalLow%Ved/Megabonk/Saves/CloudDir/<id>/" + "progression.json"
inline std::optional<fs::path> local_path(const Roots& roots,const std::string& prefix,const std::string& name) {
    if(prefix.size()<2||prefix[0]!='%') return std::nullopt;
    size_t close=prefix.find('%',1);if(close==std::string::npos) return std::nullopt;
    auto it=roots.find(prefix.substr(1,close-1));if(it==roots.end()) return std::nullopt;
    std::string relative=prefix.substr(close+1)+name;
    if(!safe_relative(relative)) return std::nullopt;
    fs::path out=it->second;
    size_t start=0;
    while(start<relative.size()){size_t end=relative.find('/',start);if(end==std::string::npos)end=relative.size();out/=relative.substr(start,end-start);start=end+1;}
    return out;
}
struct Remote { std::string name, sha; uint32_t prefix=0, raw=0; uint64_t time=0; int persist=0; };
struct Listing { bool ok=false; uint64_t change=0; std::vector<std::string> prefixes; std::vector<Remote> files; };
inline Listing list(Io& io,uint32_t appid) {
    Listing l; std::string req; pb_u(req,1,appid); pb_u(req,2,0);
    int er=0; std::string resp;
    if(!io.service("Cloud.GetAppFileChangelist#1",req,er,resp)||er!=1){io.log("cloud: changelist failed eresult="+std::to_string(er));return l;}
    for(auto& f:pb_parse(resp)) {
        if(f.f==1) l.change=f.u;
        else if(f.f==4) l.prefixes.push_back(f.s);
        else if(f.f==2) {
            Remote r;
            for(auto& x:pb_parse(f.s)) {
                if(x.f==1)r.name=x.s; else if(x.f==2)r.sha=x.s; else if(x.f==3)r.time=x.u;
                else if(x.f==4)r.raw=uint32_t(x.u); else if(x.f==5)r.persist=int(x.u); else if(x.f==7)r.prefix=uint32_t(x.u);
            }
            l.files.push_back(r);
        }
    }
    l.ok=true;return l;
}
inline std::optional<std::string> read_file(const fs::path& p) {
    std::ifstream in(p,std::ios::binary);if(!in) return std::nullopt;
    return std::string((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
}
inline bool write_file(const fs::path& p,const std::string& data) {
    std::error_code ec;fs::create_directories(p.parent_path(),ec);
    auto tmp=p;tmp+=".xboxnative-part";
    {std::ofstream out(tmp,std::ios::binary|std::ios::trunc);if(!out) return false;out.write(data.data(),std::streamsize(data.size()));if(!out) return false;}
    fs::rename(tmp,p,ec);return !ec;
}
inline uint64_t unix_time(const fs::path& p) {
    std::error_code ec;auto t=fs::last_write_time(p,ec);if(ec) return 0;
    // C++17 has no clock_cast: convert through "now" and round away the jitter.
    auto system=t-fs::file_time_type::clock::now()+std::chrono::system_clock::now();
    auto s=std::chrono::round<std::chrono::seconds>(system.time_since_epoch()).count();return s>0?uint64_t(s):0;
}
inline void set_unix_time(const fs::path& p,uint64_t seconds) {
    std::error_code ec;
    auto system=std::chrono::system_clock::time_point(std::chrono::seconds(seconds));
    fs::last_write_time(p,system-std::chrono::system_clock::now()+fs::file_time_type::clock::now(),ec);
}
struct Result { unsigned downloaded=0, uploaded=0, unchanged=0, kept=0, busy=0, failed=0; };
inline Headers headers(const std::string& message,int field) {
    Headers h;
    for(auto& f:pb_parse(message)) if(f.f==field) {
        std::string name,value;for(auto& x:pb_parse(f.s)){if(x.f==1)name=x.s;else if(x.f==2)value=x.s;}
        h.emplace_back(name,value);
    }
    return h;
}
// Before launch: bring the player's newer cloud files down.
inline Result download(Io& io,uint32_t appid,const Roots& roots) {
    Result r;auto l=list(io,appid);if(!l.ok){++r.failed;return r;}
    for(auto& file:l.files) {
        if(file.persist!=0) continue; // deleted or forgotten on the server
        if(file.prefix>=l.prefixes.size()){++r.failed;continue;}
        auto path=local_path(roots,l.prefixes[file.prefix],file.name);
        if(!path){io.log("cloud: skipped a file outside the configured roots");++r.failed;continue;}
        auto local=read_file(*path);
        if(local&&io.sha1(*local)==file.sha){++r.unchanged;continue;}
        if(local&&unix_time(*path)>=file.time){++r.kept;continue;} // local is newer: upload after play
        std::string req;pb_u(req,1,appid);pb_s(req,2,l.prefixes[file.prefix]+file.name);pb_u(req,3,1);
        int er=0;std::string resp;
        if(!io.service("Cloud.ClientFileDownload#1",req,er,resp)||er!=1){++r.failed;continue;}
        std::string host,url;bool https=true,encrypted=false;uint32_t size=0,raw=0;
        for(auto& f:pb_parse(resp)) {
            if(f.f==2)size=uint32_t(f.u);else if(f.f==3)raw=uint32_t(f.u);else if(f.f==7)host=f.s;
            else if(f.f==8)url=f.s;else if(f.f==9)https=f.u!=0;else if(f.f==11)encrypted=f.u!=0;
        }
        // Compressed or encrypted cloud copies are not handled yet: keep local.
        if(encrypted||size!=raw||host.empty()){io.log("cloud: "+file.name+" stored compressed/encrypted; not downloaded");++r.failed;continue;}
        std::string body;int status=io.get(https,host,url,headers(resp,10),body);
        if(status!=200||body.size()!=raw||io.sha1(body)!=file.sha){io.log("cloud: download of "+file.name+" failed status="+std::to_string(status));++r.failed;continue;}
        if(local){auto backup=*path;backup+=".xboxnative-backup";write_file(backup,*local);}
        if(!write_file(*path,body)){++r.failed;continue;}
        set_unix_time(*path,file.time);++r.downloaded;
    }
    return r;
}
// After exit: push new or changed files matching the save rules.
// quietSeconds: files modified more recently are left for a later pass, so a
// periodic sync never uploads a save the game is still writing.
inline Result upload(Io& io,uint32_t appid,const Roots& roots,const std::vector<Rule>& saveRules,
                     const std::string& machine,uint64_t quietSeconds=0) {
    Result r;
    const uint64_t now=uint64_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());auto l=list(io,appid);if(!l.ok){++r.failed;return r;}
    std::map<std::string,const Remote*> remote;
    for(auto& f:l.files) if(f.prefix<l.prefixes.size()) remote[l.prefixes[f.prefix]+f.name]=&f;
    struct Pending { std::string cloudName; fs::path path; std::string data; uint64_t time; };
    std::vector<Pending> pending;
    for(auto& rule:saveRules) {
        auto it=roots.find(rule.root);if(it==roots.end()) continue;
        if(!rule.path.empty()&&!safe_relative(rule.path)) continue;
        fs::path base=it->second;std::string rel=rule.path;
        for(size_t s=0;s<rel.size();){size_t e=rel.find('/',s);if(e==std::string::npos)e=rel.size();base/=rel.substr(s,e-s);s=e+1;}
        std::error_code ec;if(!fs::is_directory(base,ec)) continue;
        auto visit=[&](const fs::directory_entry& entry) {
            if(!entry.is_regular_file()) return;
            auto name=entry.path().filename().string();
            if(!glob(rule.pattern,name)||name.find(".xboxnative-")!=std::string::npos) return;
            auto sub=fs::relative(entry.path(),base,ec).generic_string();
            if(ec||!safe_relative(sub)) return;
            std::string cloudName="%"+rule.root+"%"+(rule.path.empty()?"":rule.path+"/")+sub;
            auto data=read_file(entry.path());if(!data) return;
            uint64_t time=unix_time(entry.path());
            if(quietSeconds&&time+quietSeconds>now){++r.busy;return;}
            auto found=remote.find(cloudName);
            if(found!=remote.end()&&found->second->persist==0) {
                if(io.sha1(*data)==found->second->sha){++r.unchanged;return;}
                if(time<found->second->time){++r.kept;return;} // remote is newer: keep it
            }
            pending.push_back({cloudName,entry.path(),*data,time});
        };
        if(rule.recursive){for(auto& e:fs::recursive_directory_iterator(base,ec))visit(e);}
        else for(auto& e:fs::directory_iterator(base,ec))visit(e);
    }
    if(pending.empty()) return r;
    std::string begin;pb_u(begin,1,appid);pb_s(begin,2,machine);
    for(auto& p:pending)pb_s(begin,3,p.cloudName);
    int er=0;std::string resp;
    if(!io.service("Cloud.BeginAppUploadBatch#1",begin,er,resp)||er!=1){io.log("cloud: upload batch refused eresult="+std::to_string(er));r.failed+=unsigned(pending.size());return r;}
    uint64_t batch=0;for(auto& f:pb_parse(resp))if(f.f==1)batch=f.u;
    bool all=true;
    for(auto& p:pending) {
        std::string sha=io.sha1(p.data);
        std::string req;pb_u(req,1,appid);pb_u(req,2,p.data.size());pb_u(req,3,p.data.size());pb_s(req,4,sha);
        pb_u(req,5,p.time);pb_s(req,6,p.cloudName);pb_u(req,7,0xFFFFFFFFu);pb_u(req,10,0);pb_u(req,13,batch);
        int fe=0;std::string fr;
        bool ok=io.service("Cloud.ClientBeginFileUpload#1",req,fe,fr)&&fe==1;
        if(ok) for(auto& f:pb_parse(fr)) {
            if(f.f==1&&f.u){ok=false;break;} // server wants client-side encryption: not supported
            if(f.f!=2) continue;
            std::string host,url,explicitBody;bool https=true;uint64_t offset=0;uint32_t length=0;
            for(auto& x:pb_parse(f.s)) {
                if(x.f==1)host=x.s;else if(x.f==2)url=x.s;else if(x.f==3)https=x.u!=0;
                else if(x.f==6)offset=x.u;else if(x.f==7)length=uint32_t(x.u);else if(x.f==8)explicitBody=x.s;
            }
            if(offset>p.data.size()||length>p.data.size()-offset){ok=false;break;}
            std::string body=length?p.data.substr(size_t(offset),length):explicitBody;
            int status=io.put(https,host,url,headers(f.s,5),body);
            if(status<200||status>299){ok=false;break;}
        }
        std::string commit;pb_u(commit,1,ok?1:0);pb_u(commit,2,appid);pb_s(commit,3,sha);pb_s(commit,4,p.cloudName);
        int ce=0;std::string cr;bool committed=false;
        if(io.service("Cloud.ClientCommitFileUpload#1",commit,ce,cr)&&ce==1)for(auto& f:pb_parse(cr))if(f.f==1)committed=f.u!=0;
        if(ok&&committed)++r.uploaded;else{++r.failed;all=false;io.log("cloud: upload of a save file failed");}
    }
    std::string done;pb_u(done,1,appid);pb_u(done,2,batch);pb_u(done,3,all?1:2);
    int de=0;std::string dr;io.service("Cloud.CompleteAppUploadBatchBlocking#1",done,de,dr);
    return r;
}
}
