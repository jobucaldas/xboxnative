// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the Auto-Cloud planner and wire format against a fake
// Steam service that answers with the real Cloud message shapes.
#include "../steam_cloud.h"
#include "../vendor/zlib/zlib.h"
#include <cassert>
#include <cstdio>
using namespace steam_cloud;

// What Steam stores for a compressed cloud file: a PKZip archive with one
// entry (made once with Python's zipfile; the raw file is kSaveText x 12).
static const unsigned char kZip[]={
0x50,0x4b,0x03,0x04,0x14,0x00,0x00,0x00,0x08,0x00,0x90,0x78,0x50,0x59,0x4c,0x65,0x33,0xf0,0x2a,0x00,0x00,0x00,0xa4,0x01,0x00,0x00,0x16,0x00,0x00,0x00,0x32,0x30,0x32,0x34,0x31,0x30,0x31,0x36,0x5f,0x46,0x61,0x7a,0x65,0x6e,0x64,0x61,0x5f,0x30,0x2e,0x73,0x61,0x76,0x2b,0xce,0xc9,0xcc,0x4d,0x55,0x28,0x4a,0xcc,0x4b,0xce,0x48,0x2d,0x52,0x28,0x4e,0x2c,0x4b,0x55,0x48,0x49,0x2c,0x49,0x54,0x30,0x30,0x34,0x32,0x36,0x31,0x35,0x33,0xb7,0xb0,0x54,0x28,0x1e,0x55,0x32,0xb0,0x4a,0x00,0x50,0x4b,0x01,0x02,0x14,0x03,0x14,0x00,0x00,0x00,0x08,0x00,0x90,0x78,0x50,0x59,0x4c,0x65,0x33,0xf0,0x2a,0x00,0x00,0x00,0xa4,0x01,0x00,0x00,0x16,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x01,0x00,0x00,0x00,0x00,0x32,0x30,0x32,0x34,0x31,0x30,0x31,0x36,0x5f,0x46,0x61,0x7a,0x65,0x6e,0x64,0x61,0x5f,0x30,0x2e,0x73,0x61,0x76,0x50,0x4b,0x05,0x06,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x44,0x00,0x00,0x00,0x5e,0x00,0x00,0x00,0x00,0x00};
static std::string saveText() { std::string t;for(int i=0;i<12;++i)t+="slime rancher save data 0123456789 ";return t; }
// The launcher's inflater (launch.h): raw deflate through the vendored zlib.
static bool inflater(const char* data,size_t size,size_t expected,std::string& out) {
    out.resize(expected);z_stream z{};if(inflateInit2(&z,-15)!=Z_OK) return false;
    z.next_in=(Bytef*)data;z.avail_in=(uInt)size;z.next_out=(Bytef*)&out[0];z.avail_out=(uInt)expected;
    int rc=inflate(&z,Z_FINISH);bool ok=rc==Z_STREAM_END&&z.total_out==expected;inflateEnd(&z);return ok;
}

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
    struct File { std::string data; uint64_t time=0; int persist=0; std::string name; uint32_t prefix=0;
                  std::string stored; bool encrypted=false; std::string sha;   // stored: what the CDN serves when it differs from data
                  File()=default;
                  File(std::string d,uint64_t t,int p,std::string n,uint32_t x):data(std::move(d)),time(t),persist(p),name(std::move(n)),prefix(x){} };
    bool noPrefixes=false;   // a changelist that has only API files lists no prefixes
    int apiIndex=-1;         // index of the empty prefix, when listed
    const std::string& body(const File& f) const { return f.stored.empty()?f.data:f.stored; }
    std::map<std::string,File> files;   // full cloud name -> file
    std::vector<std::string> extraPrefixes;
    std::map<std::string,std::string> uploads;   // PUT url -> body
    std::vector<std::string> committed;
    std::map<std::string,uint64_t> times;
    unsigned downloads=0, batches=0, completed=0;
    bool service(const std::string& method,const std::string& req,int& er,std::string& resp) {
        er=1;resp.clear();auto f=pb_parse(req);
        if(method=="Cloud.GetAppFileChangelist#1") {
            pb_u(resp,1,7);if(!noPrefixes)pb_s(resp,4,prefix);for(auto& p:extraPrefixes)pb_s(resp,4,p);
            for(auto& [full,file]:files) {
                std::string info;pb_s(info,1,file.name);pb_s(info,2,file.sha.empty()?sha1(file.data):file.sha);pb_u(info,3,file.time);
                pb_u(info,4,file.data.size());pb_u(info,5,file.persist);pb_u(info,7,file.prefix);pb_s(resp,2,info);
            }
            return true;
        }
        if(method=="Cloud.ClientFileDownload#1") {
            auto name=pb_get(f,2)->s;auto it=files.find(name);if(it==files.end()){er=9;return true;}
            ++downloads;
            pb_u(resp,2,body(it->second).size());pb_u(resp,3,it->second.data.size());pb_s(resp,7,"cloud.example");
            if(it->second.encrypted)pb_u(resp,11,1);
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
                if(name.rfind(prefix,0)==0) files[name]={uploads["/put/"+name],times[name],0,name.substr(prefix.size()),0};
                else {   // un-prefixed API file
                    assert(name.find('%')==std::string::npos);
                    files[name]={uploads["/put/"+name],times[name],0,name,uint32_t(apiIndex<0?0:apiIndex)};
                }
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
        body=server.body(server.files.at(path.substr(5)));return 200;};
    io.put=[&](bool https,auto& host,auto& path,auto&,auto& body){assert(https&&host=="upload.example");server.uploads[path]=body;return 200;};
    io.sha1=sha1;io.inflate=inflater;io.log=[&](auto& s){logs.push_back(s);};

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
    // Every file decision is logged with its cloud name and the local path.
    auto logged=[&](const std::string& action,const std::string& cloud,const fs::path& local){
        const std::string want="cloud: "+action+" "+cloud+" -> "+path_text(local);
        for(auto& l:logs) if(l==want) return true;
        return false;};
    assert(logged("downloaded",server.prefix+"missing.json",dir/"missing.json"));
    assert(logged("downloaded",server.prefix+"remotenewer.json",dir/"remotenewer.json"));
    assert(logged("unchanged",server.prefix+"same.json",dir/"same.json"));
    assert(logged("keep-local (newer here; uploads after play)",server.prefix+"localnewer.json",dir/"localnewer.json"));
    bool sawDeleted=false,sawOutside=false;
    for(auto& l:logs){sawDeleted|=l=="cloud: skipped "+server.prefix+"gone.json (deleted or forgotten on the server)";
                      sawOutside|=l=="cloud: skipped %WinAppDataLocalLow%../../evil.json (not under a configured root)";}
    assert(sawDeleted&&sawOutside);
    // A dry pass only counts what would come down.
    {   server.add("dry.json","dry",1700002000);
        auto before=server.downloads;
        auto dry=download(io,3405340,roots,true);
        assert(dry.newer==1&&dry.downloaded==0&&server.downloads==before&&!fs::exists(dir/"dry.json"));
        server.files.erase(server.prefix+"dry.json");}

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

    // ---- compressed and encrypted cloud copies
    {   const std::string zip(reinterpret_cast<const char*>(kZip),sizeof kZip),text=saveText();
        std::string inflated;
        assert(steam_client_package::zip_member(zip,nullptr,inflater,inflated)&&inflated==text);
        assert(steam_client_package::zip_member(zip,"20241016_Fazenda_0.sav",inflater,inflated)&&inflated==text);
        assert(!steam_client_package::zip_member(zip,"other.sav",inflater,inflated));
        auto torn=zip;torn[104]=99;   // central directory: unknown compression method
        assert(!steam_client_package::zip_member(torn,nullptr,inflater,inflated));
        assert(!steam_client_package::zip_member(zip.substr(0,zip.size()-10),nullptr,inflater,inflated));

        auto crypt=fs::temp_directory_path()/"steam-cloud-test-zip";fs::remove_all(crypt);
        Roots zroots{{"WinAppDataLocalLow",crypt}};
        Server zs;zs.prefix="%WinAppDataLocalLow%Mp/SR/";
        Io zio=io;
        zio.service=[&](auto& m,auto& r,int& e,std::string& o){return zs.service(m,r,e,o);};
        zio.get=[&](bool,auto&,auto& path,auto&,std::string& body){body=zs.body(zs.files.at(path.substr(5)));return 200;};
        logs.clear();
        zs.add("ok.sav",text,1700000000);zs.files[zs.prefix+"ok.sav"].stored=zip;
        auto z1=download(zio,433340,zroots);
        assert(z1.downloaded==1&&z1.failed==0&&*read_file(crypt/"Mp"/"SR"/"ok.sav")==text);
        assert(unix_time(crypt/"Mp"/"SR"/"ok.sav")==1700000000);
        assert(logged("downloaded (inflated)",zs.prefix+"ok.sav",crypt/"Mp"/"SR"/"ok.sav"));
        // The inflated bytes must match the changelist SHA-1.
        zs.files.clear();logs.clear();
        zs.add("bad.sav",text,1700000000);zs.files[zs.prefix+"bad.sav"].stored=zip;zs.files[zs.prefix+"bad.sav"].sha=sha1("something else");
        auto z2=download(zio,433340,zroots);
        assert(z2.downloaded==0&&z2.failed==1&&!fs::exists(crypt/"Mp"/"SR"/"bad.sav"));
        assert(logged("fail (SHA-1 differs from the changelist)",zs.prefix+"bad.sav",crypt/"Mp"/"SR"/"bad.sav"));
        // A corrupt archive, a wrong raw size, and an encrypted copy are all left alone.
        zs.files.clear();logs.clear();
        zs.add("torn.sav",text,1700000000);zs.files[zs.prefix+"torn.sav"].stored=torn;
        zs.add("short.sav",text+"x",1700000000);zs.files[zs.prefix+"short.sav"].stored=zip;   // raw size claims 421 bytes
        zs.add("enc.sav",text,1700000000);zs.files[zs.prefix+"enc.sav"].encrypted=true;
        auto z3=download(zio,433340,zroots);
        assert(z3.downloaded==0&&z3.failed==3&&!fs::exists(crypt/"Mp"/"SR"/"torn.sav")&&!fs::exists(crypt/"Mp"/"SR"/"short.sav")&&!fs::exists(crypt/"Mp"/"SR"/"enc.sav"));
        assert(logged("fail (compressed copy did not inflate)",zs.prefix+"torn.sav",crypt/"Mp"/"SR"/"torn.sav"));
        assert(logged("skip (stored encrypted; client-side encryption unsupported)",zs.prefix+"enc.sav",crypt/"Mp"/"SR"/"enc.sav"));
        fs::remove_all(crypt);
    }

    // ---- appinfo rule paths use '\\'; cloud names use '/'
    {   Vdf winApp;assert(vdf_parse("\"appinfo\"{\"ufs\"{\"savefiles\"{\"0\"{\"root\" \"WinAppDataLocalLow\" \"path\" \"TheGameBakers\\\\Haven\" \"pattern\" \"*\" \"recursive\" \"1\"}}}}",winApp));
        auto haven=rules(*winApp.get("appinfo"));
        assert(haven.size()==1&&haven[0].path=="TheGameBakers/Haven"&&haven[0].recursive);
    }

    // ---- ISteamRemoteStorage API files: no prefix, gbe's remote folder
    {   auto api=fs::temp_directory_path()/"steam-cloud-test-api";fs::remove_all(api);
        auto remote=api/"saves"/"459220"/"remote";
        Roots aroots{{kRemoteRoot,remote},{"WinAppDataLocalLow",api/"low"}};
        assert(local_path(aroots,"","profile.sav")==remote/"profile.sav");
        assert(local_path(aroots,"","slot/1.sav")==remote/"slot"/"1.sav");
        assert(!local_path(aroots,"","../x")&&!local_path(aroots,"","/abs")&&!local_path(aroots,"","C:x"));
        assert(!local_path(Roots{{"WinAppDataLocalLow",api/"low"}},"","profile.sav"));   // no remote folder configured
        assert(backup_path(aroots,remote/"slot"/"1.sav")==api/"saves"/"459220"/"remote.xboxnative-backup"/"slot"/"1.sav");
        assert(backup_path(aroots,api/"low"/"a.sav")==api/"low"/"a.sav.xboxnative-backup");

        Server as;as.noPrefixes=true;
        Io aio=io;
        aio.service=[&](auto& m,auto& r,int& e,std::string& o){return as.service(m,r,e,o);};
        aio.get=[&](bool,auto&,auto& path,auto&,std::string& body){body=as.body(as.files.at(path.substr(5)));return 200;};
        aio.put=[&](bool,auto&,auto& path,auto&,auto& body){as.uploads[path]=body;return 200;};
        as.files["profile.sav"]={"halo-profile",1700000000,0,"profile.sav",0};
        as.files["slot/1.sav"]={"slot-one",1700000100,0,"slot/1.sav",0};
        logs.clear();
        auto a1=download(aio,459220,aroots);
        assert(a1.downloaded==2&&a1.failed==0&&*read_file(remote/"profile.sav")=="halo-profile"&&*read_file(remote/"slot"/"1.sav")=="slot-one");
        assert(logged("downloaded","profile.sav",remote/"profile.sav"));
        // The empty prefix may also be listed explicitly.
        as.noPrefixes=false;as.prefix="";
        write_file(remote/"profile.sav","stale");set_unix_time(remote/"profile.sav",1600000000);
        auto a2=download(aio,459220,aroots);
        assert(a2.downloaded==1&&a2.unchanged==1&&*read_file(remote/"profile.sav")=="halo-profile");
        assert(*read_file(api/"saves"/"459220"/"remote.xboxnative-backup"/"profile.sav")=="stale");
        assert(!fs::exists(remote/"profile.sav.xboxnative-backup"));   // the game never sees backups as cloud files
        // A changed or new API file goes up under its plain name, no Auto-Cloud rules needed.
        as.prefix="%WinAppDataLocalLow%Unused/";as.extraPrefixes={""};as.apiIndex=1;
        for(auto& [name,file]:as.files) file.prefix=1;
        write_file(remote/"profile.sav","halo-profile-2");set_unix_time(remote/"profile.sav",1700000500);
        write_file(remote/"new.sav","fresh");set_unix_time(remote/"new.sav",1700000600);
        auto a3=upload(aio,459220,aroots,{},"Xbox");
        assert(a3.uploaded==2&&a3.failed==0&&as.batches==1);
        assert(as.uploads["/put/profile.sav"]=="halo-profile-2"&&as.uploads["/put/new.sav"]=="fresh"&&as.uploads.size()==2);
        assert(as.files.at("new.sav").prefix==1&&as.files.count("%WinAppDataLocalLow%Unused/new.sav")==0);
        assert(logged("upload","new.sav",remote/"new.sav"));
        fs::remove_all(api);
    }
    fs::remove_all(root);fs::remove_all(outside);
    // ---- last-sync baseline (Known): a console that never synced a file takes
    // the cloud copy; afterwards only the side that changed wins, and a file
    // changed on both sides moves only when the player picks a side.
    {   auto kroot=fs::temp_directory_path()/"steam-cloud-known";fs::remove_all(kroot);
        Roots kroots{{"WinAppDataLocalLow",kroot}};
        auto kdir=kroot/"Ved"/"Megabonk"/"Saves"/"CloudDir"/"1";fs::create_directories(kdir);
        Server ks;Io kio=io;
        kio.service=[&](auto& m,auto& r,int& e,std::string& o){return ks.service(m,r,e,o);};
        kio.get=[&](bool,auto&,auto& path,auto&,std::string& body){body=ks.body(ks.files.at(path.substr(5)));return 200;};
        kio.put=[&](bool,auto&,auto& path,auto&,auto& body){ks.uploads[path]=body;return 200;};
        auto put=[&](const char* name,const std::string& data,uint64_t time){write_file(kdir/name,data);set_unix_time(kdir/name,time);};
        Known known;
        // Fresh local file written by the game, newer than the player's cloud save: cloud wins, local backed up.
        ks.add("save.json","pc progress",1700000000);put("save.json","new profile",1800000000);
        auto first=download(kio,1,kroots,false,&known);
        assert(first.downloaded==1&&*read_file(kdir/"save.json")=="pc progress"&&*read_file(kdir/"save.json.xboxnative-backup")=="new profile");
        assert(known.at(ks.prefix+"save.json")==sha1("pc progress"));
        // Only the local copy changed: kept, then uploaded.
        put("save.json","played on xbox",1800000100);
        auto keep=download(kio,1,kroots,false,&known);assert(keep.kept==1&&keep.downloaded==0);
        auto push=upload(kio,1,kroots,saveRules,"Xbox",0,&known);
        assert(push.uploaded==1&&known.at(ks.prefix+"save.json")==sha1("played on xbox"));
        // Only the cloud changed: downloaded even though the local file looks newer.
        ks.add("save.json","played on pc",1700000000);set_unix_time(kdir/"save.json",1900000000);
        auto pull=download(kio,1,kroots,false,&known);assert(pull.downloaded==1&&*read_file(kdir/"save.json")=="played on pc");
        // Both changed: conflict, nothing moves either way.
        ks.add("save.json","pc again",1700000000);put("save.json","xbox again",1900000000);
        auto c1=download(kio,1,kroots,false,&known);auto c2=upload(kio,1,kroots,saveRules,"Xbox",0,&known);
        assert(c1.conflict==1&&c2.conflict==1&&c2.uploaded==0&&*read_file(kdir/"save.json")=="xbox again"&&ks.files.at(ks.prefix+"save.json").data=="pc again");
        // The player picks: the cloud copy, or this console's.
        auto useCloud=download(kio,1,kroots,false,&known,Prefer::Cloud);
        assert(useCloud.downloaded==1&&*read_file(kdir/"save.json")=="pc again");
        put("save.json","xbox wins",1900000000);ks.add("save.json","pc later",1700000000);
        auto keepXbox=upload(kio,1,kroots,saveRules,"Xbox",0,&known,Prefer::Local);
        assert(keepXbox.uploaded==1&&ks.files.at(ks.prefix+"save.json").data=="xbox wins");
        // Upload never replaces a cloud file this console never synced.
        put("save.json","another console run",1900000500);
        Known empty;auto hold=upload(kio,1,kroots,saveRules,"Xbox",0,&empty);
        assert(hold.conflict==1&&hold.uploaded==0);
        fs::remove_all(kroot);}
    puts("STEAM-CLOUD PASS: rules, last-sync baseline/conflicts, paths, download policy/backup/traversal, per-file log, zip inflate+SHA-1, encrypted skip, API remote folder, upload batch/blocks/commit");
}
