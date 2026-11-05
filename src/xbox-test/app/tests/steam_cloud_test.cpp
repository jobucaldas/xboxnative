// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the Auto-Cloud planner and wire format against a fake
// Steam service that answers with the real Cloud message shapes.
#include "../steam_cloud.h"
#include <cassert>
#include <cstdio>
using namespace steam_cloud;

static std::string sha1(const std::string& data) {   // FIPS 180-1, test only
    uint32_t h[5]={0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0};
    std::string m=data;uint64_t bits=uint64_t(data.size())*8;m+=char(0x80);
    while(m.size()%64!=56)m+=char(0);
    for(int i=7;i>=0;--i)m+=char(bits>>(i*8));
    auto rol=[](uint32_t v,int n){return (v<<n)|(v>>(32-n));};
    for(size_t c=0;c<m.size();c+=64) {
        uint32_t w[80];
        for(int i=0;i<16;++i)w[i]=uint32_t(uint8_t(m[c+i*4]))<<24|uint32_t(uint8_t(m[c+i*4+1]))<<16|uint32_t(uint8_t(m[c+i*4+2]))<<8|uint8_t(m[c+i*4+3]);
        for(int i=16;i<80;++i)w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        uint32_t a=h[0],b=h[1],cc=h[2],d=h[3],e=h[4];
        for(int i=0;i<80;++i) {
            uint32_t f,k;
            if(i<20){f=(b&cc)|(~b&d);k=0x5A827999;}else if(i<40){f=b^cc^d;k=0x6ED9EBA1;}
            else if(i<60){f=(b&cc)|(b&d)|(cc&d);k=0x8F1BBCDC;}else{f=b^cc^d;k=0xCA62C1D6;}
            uint32_t t=rol(a,5)+f+e+k+w[i];e=d;d=cc;cc=rol(b,30);b=a;a=t;
        }
        h[0]+=a;h[1]+=b;h[2]+=cc;h[3]+=d;h[4]+=e;
    }
    std::string out;for(auto v:h)for(int i=3;i>=0;--i)out+=char(v>>(i*8));
    return out;
}
struct Server {
    std::string prefix="%WinAppDataLocalLow%Ved/Megabonk/Saves/CloudDir/1/";
    struct File { std::string data; uint64_t time; int persist=0; std::string name; uint32_t prefix=0; };
    std::map<std::string,File> files;   // full cloud name -> file
    std::vector<std::string> extraPrefixes;
    std::map<std::string,std::string> uploads;   // PUT url -> body
    std::vector<std::string> committed;
    std::map<std::string,uint64_t> times;
    unsigned downloads=0, batches=0, completed=0;
    bool service(const std::string& method,const std::string& req,int& er,std::string& resp) {
        er=1;resp.clear();auto f=pb_parse(req);
        if(method=="Cloud.GetAppFileChangelist#1") {
            pb_u(resp,1,7);pb_s(resp,4,prefix);for(auto& p:extraPrefixes)pb_s(resp,4,p);
            for(auto& [full,file]:files) {
                std::string info;pb_s(info,1,file.name);pb_s(info,2,sha1(file.data));pb_u(info,3,file.time);
                pb_u(info,4,file.data.size());pb_u(info,5,file.persist);pb_u(info,7,file.prefix);pb_s(resp,2,info);
            }
            return true;
        }
        if(method=="Cloud.ClientFileDownload#1") {
            auto name=pb_get(f,2)->s;auto it=files.find(name);if(it==files.end()){er=9;return true;}
            ++downloads;
            pb_u(resp,2,it->second.data.size());pb_u(resp,3,it->second.data.size());pb_s(resp,7,"cloud.example");
            pb_s(resp,8,"/get/"+name);pb_u(resp,9,1);
            std::string h;pb_s(h,1,"x-test");pb_s(h,2,"1");pb_s(resp,10,h);
            return true;
        }
        if(method=="Cloud.BeginAppUploadBatch#1"){++batches;pb_u(resp,1,55);return true;}
        if(method=="Cloud.ClientBeginFileUpload#1") {
            assert(pb_get(f,13)->u==55);
            auto name=pb_get(f,6)->s;uint64_t size=pb_get(f,2)->u;times[name]=pb_get(f,5)->u;
            std::string block;pb_s(block,1,"upload.example");pb_s(block,2,"/put/"+name);pb_u(block,3,1);
            pb_u(block,6,0);pb_u(block,7,size);pb_s(resp,2,block);
            return true;
        }
        if(method=="Cloud.ClientCommitFileUpload#1") {
            bool ok=pb_get(f,1)->u!=0;auto name=pb_get(f,4)->s;
            if(ok&&sha1(uploads["/put/"+name])==pb_get(f,3)->s) {
                committed.push_back(name);
                assert(name.rfind(prefix,0)==0);
                files[name]={uploads["/put/"+name],times[name],0,name.substr(prefix.size()),0};
            }
            pb_u(resp,1,ok);return true;
        }
        if(method=="Cloud.CompleteAppUploadBatchBlocking#1"){++completed;return true;}
        return false;
    }
    void add(const std::string& name,const std::string& data,uint64_t time,int persist=0) {
        files[prefix+name]={data,time,persist,name,0};
    }
};
int main() {
    assert(glob("*.json","stats.json")&&!glob("*.json","stats.json.bak")&&glob("*","x")&&glob("a?c","abc"));
    assert(safe_relative("Ved/Megabonk/a.json")&&!safe_relative("../x")&&!safe_relative("a/../b")&&
           !safe_relative("/abs")&&!safe_relative("C:x")&&!safe_relative(""));
    Vdf app;assert(vdf_parse("\"appinfo\"{\"ufs\"{\"savefiles\"{\"0\"{\"root\" \"WinAppDataLocalLow\" \"path\" \"Ved/Megabonk/Saves/CloudDir\" \"pattern\" \"*.json\" \"recursive\" \"1\"}\"1\"{\"root\" \"MacAppSupport\" \"path\" \"x\"}}}}",app));
    auto saveRules=rules(*app.get("appinfo"));
    assert(saveRules.size()==1&&saveRules[0].pattern=="*.json"&&saveRules[0].recursive);

    auto root=fs::temp_directory_path()/"steam-cloud-test";fs::remove_all(root);
    Roots roots{{"WinAppDataLocalLow",root}};
    auto dir=root/"Ved"/"Megabonk"/"Saves"/"CloudDir"/"1";fs::create_directories(dir);
    assert(local_path(roots,"%WinAppDataLocalLow%Ved/","x.json")==root/"Ved"/"x.json");
    assert(!local_path(roots,"%WinAppDataLocalLow%../","x.json")&&!local_path(roots,"%Other%a/","x")&&!local_path(roots,"WinAppDataLocalLow/","x"));

    Server server;std::vector<std::string> logs;
    Io io;
    io.service=[&](auto& m,auto& r,int& e,std::string& o){return server.service(m,r,e,o);};
    io.get=[&](bool https,auto& host,auto& path,auto& h,std::string& body){
        assert(https&&host=="cloud.example"&&h.size()==1&&h[0].first=="x-test");
        body=server.files.at(path.substr(5)).data;return 200;};
    io.put=[&](bool https,auto& host,auto& path,auto&,auto& body){assert(https&&host=="upload.example");server.uploads[path]=body;return 200;};
    io.sha1=sha1;io.log=[&](auto& s){logs.push_back(s);};

    auto write=[&](const char* name,const std::string& data,uint64_t time){write_file(dir/name,data);set_unix_time(dir/name,time);};
    server.add("missing.json","remote-new",1700000100);           // not local: download
    server.add("same.json","same",1700000000);write("same.json","same",1600000000);  // identical: unchanged
    server.add("localnewer.json","old",1700000000);write("localnewer.json","mine",1700000500); // keep local
    server.add("remotenewer.json","theirs",1700000900);write("remotenewer.json","stale",1700000000); // download + backup
    server.add("gone.json","x",1700000000,1);                     // deleted on server: ignored
    server.extraPrefixes={"%WinAppDataLocalLow%../../"};
    server.files["%WinAppDataLocalLow%../../evil.json"]={"evil",1700000000,0,"evil.json",1};

    auto down=download(io,3405340,roots);
    assert(down.downloaded==2&&down.unchanged==1&&down.kept==1&&down.failed==1);
    assert(*read_file(dir/"missing.json")=="remote-new"&&unix_time(dir/"missing.json")==1700000100);
    assert(*read_file(dir/"remotenewer.json")=="theirs"&&*read_file(dir/"remotenewer.json.xboxnative-backup")=="stale");
    assert(*read_file(dir/"localnewer.json")=="mine"&&!fs::exists(dir/"gone.json"));
    assert(!fs::exists(root.parent_path()/"evil.json")&&server.downloads==2);

    // After play: localnewer.json is newer and changed, a new file appears, a
    // non-matching file and the backup are ignored, unchanged files stay put.
    server.files.erase("%WinAppDataLocalLow%../../evil.json");server.extraPrefixes.clear();
    fs::create_directories(dir/"slot");write("slot/run.json","run",1700001000);write("notes.txt","x",1700001000);
    auto up=upload(io,3405340,roots,saveRules,"Xbox");
    assert(up.uploaded==2&&up.failed==0&&server.batches==1&&server.completed==1);
    assert(server.committed.size()==2&&server.uploads["/put/"+server.prefix+"localnewer.json"]=="mine");
    assert(server.uploads.count("/put/"+server.prefix+"slot/run.json")&&server.uploads.size()==2);
    // Links inside the save root are never followed: not uploaded, not
    // downloaded through.
    auto outside=root.parent_path()/"steam-cloud-outside";fs::create_directories(outside);
    write_file(outside/"secret.json","outside");
    fs::create_symlink(outside/"secret.json",dir/"link.json");
    fs::create_directory_symlink(outside,dir/"linkdir");
    assert(!local_path(roots,"%WinAppDataLocalLow%Ved/Megabonk/Saves/CloudDir/1/","linkdir/x.json"));
    auto again=upload(io,3405340,roots,saveRules,"Xbox");      // nothing new: no batch
    assert(again.uploaded==0&&server.batches==1);
    // A save written just now waits for a later pass; an older one goes up.
    auto nowSeconds=uint64_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    write("fresh.json","fresh",nowSeconds);
    auto quiet=upload(io,3405340,roots,saveRules,"Xbox",30);
    assert(quiet.uploaded==0&&quiet.busy==1&&server.batches==1);
    set_unix_time(dir/"fresh.json",nowSeconds-60);
    auto later=upload(io,3405340,roots,saveRules,"Xbox",30);
    assert(later.uploaded==1&&later.busy==0&&server.batches==2);
    // Save-triggered sync: the signature changes when a matching save changes,
    // not for files outside the rules.
    auto before=signature(roots,saveRules);
    write("notes.txt","changed",nowSeconds+5);
    assert(signature(roots,saveRules)==before);
    write("slot/run.json","run2",nowSeconds+5);
    assert(signature(roots,saveRules)!=before);
    auto extraDir=root/"extra";write_file(extraDir/"achievements.json","{}");
    auto withExtra=signature(roots,saveRules,{extraDir});
    write_file(extraDir/"achievements.json","{\"A\":{}}");
    assert(signature(roots,saveRules,{extraDir})!=withExtra);
    fs::remove_all(root);fs::remove_all(outside);
    puts("STEAM-CLOUD PASS: rules, paths, download policy/backup/traversal, upload batch/blocks/commit");
}
