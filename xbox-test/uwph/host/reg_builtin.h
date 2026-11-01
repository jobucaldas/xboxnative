#pragma once
// uwph registry builtin: the nine advapi32 Reg* imports of UnityPlayer.dll, backed by a SQLite
// database (LocalState\registry.sqlite, WAL, one table keyed by (key path, value name)). The old
// LocalState\registry.json (format uwph-registry/1) is imported once, then renamed *.imported. The console refuses RegCreateKey*/Set* under HKCU
// (ERROR_ACCESS_DENIED, see LANE_NOTES.md), so guests get this instead of the real advapi32.
// Same signatures and Win32 error codes as advapi32 for the paths Unity PlayerPrefs uses.
#include <windows.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <cstdarg>
#include "sqlite3.h"
namespace regb {
struct Value { std::wstring name; DWORD type=REG_NONE; std::vector<BYTE> data; };
struct Key { std::wstring path; std::map<std::wstring,Value> values; };  // values keyed by lowercase name
static std::mutex lock;
static std::wstring file;     // legacy registry.json (one-time import source)
static std::wstring dbfile;   // registry.sqlite
static sqlite3* db=nullptr;
static bool db_ok=false;      // false: sqlite unavailable, registry works in memory only (logged loudly)
static std::map<std::wstring,Key> keys;          // lowercase full path "hkcu\software\..." -> key
static std::map<uintptr_t,std::wstring> handles;  // fake handle -> lowercase full path
static uintptr_t next_handle=0x5ED00004;
static bool loaded=false;
static void (*logger)(const char*)=nullptr;
static void note(const char* m) { if(logger) logger(m); }

static std::wstring lower(std::wstring s) { for(auto& c:s) c=(wchar_t)towlower(c); return s; }
static std::string utf8(const std::wstring& w) {
    if(w.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);
    std::string s(n,'\0'); WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),&s[0],n,nullptr,nullptr); return s;
}
static std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);
    std::wstring w(n,L'\0'); MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),&w[0],n); return w;
}
static const char* root_name(uintptr_t h) {
    switch(h) { case 0x80000000u: return "HKCR"; case 0x80000001u: return "HKCU"; case 0x80000002u: return "HKLM"; case 0x80000003u: return "HKU"; }
    return nullptr;
}
// ---- persistence: {"format":"uwph-registry/1","keys":[{"path":..,"values":[{"name":..,"type":N,"hex":".."}]}]}
static void esc(std::string& o,const std::string& s) {
    o+='"';
    for(unsigned char c:s) {
        if(c=='"'||c=='\\') { o+='\\'; o+=(char)c; }
        else if(c<0x20) { char b[8]; snprintf(b,sizeof b,"\\u%04x",c); o+=b; }
        else o+=(char)c;
    }
    o+='"';
}
// ---- SQLite storage. One table, one row per key marker (is_val=0) or per value (is_val=1).
// Every public mutation is ONE transaction (WAL, synchronous=FULL), so a kill mid-write leaves
// either the whole change or none of it. Memory (`keys`) is a write-through cache of the table.
static bool sql(const char* q) {
    char* e=nullptr; int rc=sqlite3_exec(db,q,nullptr,nullptr,&e);
    if(rc!=SQLITE_OK) { char m[320]; snprintf(m,sizeof m,"REGB sql error %d: %s | %.60s",rc,e?e:"?",q); note(m); }
    sqlite3_free(e); return rc==SQLITE_OK;
}
static std::string pragma_text(const char* q) {
    std::string r; sqlite3_stmt* st=nullptr;
    if(sqlite3_prepare_v2(db,q,-1,&st,nullptr)==SQLITE_OK) { if(sqlite3_step(st)==SQLITE_ROW && sqlite3_column_text(st,0)) r=(const char*)sqlite3_column_text(st,0); }
    sqlite3_finalize(st); return r;
}
static const char* SCHEMA=
    "CREATE TABLE IF NOT EXISTS reg("
    "key_lc TEXT NOT NULL,"       // lowercase full key path, e.g. hkcu\\software\\ved\\megabonk
    "val_lc TEXT NOT NULL,"       // lowercase value name ('' = default value); '' for key marker rows
    "is_val INTEGER NOT NULL,"    // 0 = key marker row, 1 = value row
    "key_path TEXT NOT NULL,"     // original-case key path
    "val_name TEXT,"              // original-case value name
    "type INTEGER,"               // REG_* type
    "data BLOB,"                  // raw value bytes exactly as stored by the guest
    "PRIMARY KEY(key_lc,val_lc,is_val)) WITHOUT ROWID;";
struct Txn {
    bool open=false;
    Txn() { if(db_ok) open=sql("BEGIN IMMEDIATE"); }
    bool commit() { if(!open) return false; open=false; if(sql("COMMIT")) return true; sql("ROLLBACK"); return false; }
    ~Txn() { if(open) sql("ROLLBACK"); }
};
static bool put_key_row(const std::wstring& lk,const std::wstring& path) {
    sqlite3_stmt* st=nullptr;
    if(sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO reg(key_lc,val_lc,is_val,key_path) VALUES(?1,'',0,?2)",-1,&st,nullptr)!=SQLITE_OK) return false;
    std::string a=utf8(lk), b=utf8(path);
    sqlite3_bind_text(st,1,a.data(),(int)a.size(),SQLITE_TRANSIENT); sqlite3_bind_text(st,2,b.data(),(int)b.size(),SQLITE_TRANSIENT);
    bool ok=sqlite3_step(st)==SQLITE_DONE; sqlite3_finalize(st); return ok;
}
static bool put_value_row(const std::wstring& lk,const std::wstring& path,const Value& v) {
    sqlite3_stmt* st=nullptr;
    if(sqlite3_prepare_v2(db,"INSERT OR REPLACE INTO reg(key_lc,val_lc,is_val,key_path,val_name,type,data) VALUES(?1,?2,1,?3,?4,?5,?6)",-1,&st,nullptr)!=SQLITE_OK) return false;
    std::string a=utf8(lk), b=utf8(lower(v.name)), c=utf8(path), d=utf8(v.name);
    sqlite3_bind_text(st,1,a.data(),(int)a.size(),SQLITE_TRANSIENT); sqlite3_bind_text(st,2,b.data(),(int)b.size(),SQLITE_TRANSIENT);
    sqlite3_bind_text(st,3,c.data(),(int)c.size(),SQLITE_TRANSIENT); sqlite3_bind_text(st,4,d.data(),(int)d.size(),SQLITE_TRANSIENT);
    sqlite3_bind_int64(st,5,(sqlite3_int64)v.type); sqlite3_bind_blob(st,6,v.data.empty()?"":(const void*)v.data.data(),(int)v.data.size(),SQLITE_TRANSIENT);
    bool ok=sqlite3_step(st)==SQLITE_DONE; sqlite3_finalize(st); return ok;
}
static void db_put_keys(const std::vector<std::pair<std::wstring,std::wstring>>& ks) {   // (lowercase path, display path), one txn
    if(!db_ok) return; Txn t; if(!t.open) return;
    for(auto& k:ks) if(!put_key_row(k.first,k.second)) { note("REGB db: key insert failed"); return; }
    if(!t.commit()) note("REGB db: key commit failed");
}
static void db_put_value(const std::wstring& lk,const Key& k,const Value& v) {
    if(!db_ok) return; Txn t; if(!t.open) return;
    if(!put_key_row(lk,k.path) || !put_value_row(lk,k.path,v)) { note("REGB db: value write failed"); return; }
    if(!t.commit()) note("REGB db: value commit failed");
}
static void db_del_value(const std::wstring& lk,const std::wstring& lname) {
    if(!db_ok) return; sqlite3_stmt* st=nullptr;
    if(sqlite3_prepare_v2(db,"DELETE FROM reg WHERE key_lc=?1 AND val_lc=?2 AND is_val=1",-1,&st,nullptr)!=SQLITE_OK) { note("REGB db: delete prepare failed"); return; }
    std::string a=utf8(lk), b=utf8(lname);
    sqlite3_bind_text(st,1,a.data(),(int)a.size(),SQLITE_TRANSIENT); sqlite3_bind_text(st,2,b.data(),(int)b.size(),SQLITE_TRANSIENT);
    if(sqlite3_step(st)!=SQLITE_DONE) note("REGB db: delete failed"); sqlite3_finalize(st);   // single statement = implicit transaction
}
struct Parser {
    const std::string& s; size_t i=0; bool bad=false;
    explicit Parser(const std::string& t):s(t) {}
    void ws() { while(i<s.size() && (s[i]==' '||s[i]=='\n'||s[i]=='\r'||s[i]=='\t')) i++; }
    bool eat(char c) { ws(); if(i<s.size()&&s[i]==c) { i++; return true; } return false; }
    std::string str() {
        std::string o; ws(); if(i>=s.size()||s[i]!='"') { bad=true; return o; } i++;
        while(i<s.size() && s[i]!='"') {
            char c=s[i++];
            if(c!='\\') { o+=c; continue; }
            if(i>=s.size()) { bad=true; break; }
            char e=s[i++];
            switch(e) {
                case 'n': o+='\n'; break; case 'r': o+='\r'; break; case 't': o+='\t'; break; case 'b': o+='\b'; break; case 'f': o+='\f'; break;
                case 'u': { if(i+4>s.size()) { bad=true; break; } unsigned v=(unsigned)strtoul(s.substr(i,4).c_str(),nullptr,16); i+=4;
                    std::wstring w(1,(wchar_t)v); o+=utf8(w); break; }
                default: o+=e;
            }
        }
        if(i>=s.size()) bad=true; else i++;
        return o;
    }
    void skip() { // any value
        ws(); if(i>=s.size()) { bad=true; return; }
        if(s[i]=='"') { str(); return; }
        if(s[i]=='{') { i++; if(eat('}')) return; do { str(); if(!eat(':')) { bad=true; return; } skip(); } while(!bad && eat(',')); if(!eat('}')) bad=true; return; }
        if(s[i]=='[') { i++; if(eat(']')) return; do skip(); while(!bad && eat(',')); if(!eat(']')) bad=true; return; }
        while(i<s.size() && !strchr(",}] \n\r\t",s[i])) i++;
    }
};
// Legacy registry.json -> `keys`. Returns true when the file existed and parsed completely.
static bool parse_json_file() {
    keys.clear();
    FILE* f=_wfopen(file.c_str(),L"rb"); if(!f) return false;
    std::string t; char b[4096]; size_t n; while((n=fread(b,1,sizeof b,f))>0) t.append(b,n); fclose(f);
    Parser p(t);
    if(!p.eat('{')) { note("REGB import: registry.json is not JSON; ignored"); return false; }
    do {
        std::string k=p.str(); if(!p.eat(':')) { p.bad=true; break; }
        if(k!="keys") { p.skip(); continue; }
        if(!p.eat('[')) { p.bad=true; break; }
        if(p.eat(']')) continue;
        do {
            Key key; if(!p.eat('{')) { p.bad=true; break; }
            do {
                std::string kk=p.str(); if(!p.eat(':')) { p.bad=true; break; }
                if(kk=="path") key.path=wide(p.str());
                else if(kk=="values") {
                    if(!p.eat('[')) { p.bad=true; break; }
                    bool empty=p.eat(']');
                    if(!empty) do {
                        Value v; if(!p.eat('{')) { p.bad=true; break; }
                        do {
                            std::string vk=p.str(); if(!p.eat(':')) { p.bad=true; break; }
                            if(vk=="name") v.name=wide(p.str());
                            else if(vk=="type") { p.ws(); v.type=(DWORD)strtoul(t.c_str()+p.i,nullptr,10); p.skip(); }
                            else if(vk=="hex") { std::string h=p.str(); for(size_t j=0;j+1<h.size();j+=2) v.data.push_back((BYTE)strtoul(h.substr(j,2).c_str(),nullptr,16)); }
                            else p.skip();
                        } while(!p.bad && p.eat(','));
                        if(!p.eat('}')) p.bad=true;
                        key.values[lower(v.name)]=v;
                    } while(!p.bad && p.eat(','));
                    if(!empty && !p.bad && !p.eat(']')) p.bad=true;
                } else p.skip();
            } while(!p.bad && p.eat(','));
            if(!p.eat('}')) p.bad=true;
            if(!p.bad && !key.path.empty()) keys[lower(key.path)]=key;
        } while(!p.bad && p.eat(','));
        if(!p.bad && !p.eat(']')) p.bad=true;
    } while(!p.bad && p.eat(','));
    if(p.bad) { note("REGB import: malformed registry.json; ignored"); keys.clear(); return false; }
    return true;
}
static void open_db() {
    if(db) { sqlite3_close(db); db=nullptr; } db_ok=false;
    std::string path=utf8(dbfile);
    for(int attempt=0;attempt<2;attempt++) {   // attempt 0: normal WAL (shared wal-index); attempt 1: WAL with EXCLUSIVE locking (wal-index in heap, no shm mapping)
        if(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX,nullptr)!=SQLITE_OK) {
            char m[300]; snprintf(m,sizeof m,"REGB sqlite open failed: %s",db?sqlite3_errmsg(db):"no handle"); note(m); sqlite3_close(db); db=nullptr; return; }
        sqlite3_busy_timeout(db,2000);
        if(attempt==1) sql("PRAGMA locking_mode=EXCLUSIVE");
        std::string jm=pragma_text("PRAGMA journal_mode=WAL");
        bool ok=jm=="wal" && sql("PRAGMA synchronous=FULL") && sql(SCHEMA);
        if(ok) {
            db_ok=true;
            char m[200]; snprintf(m,sizeof m,"REGB sqlite %s journal_mode=%s synchronous=FULL locking=%s db=registry.sqlite",sqlite3_libversion(),jm.c_str(),attempt?"exclusive":"normal"); note(m);
            return;
        }
        char m[200]; snprintf(m,sizeof m,"REGB sqlite WAL setup failed (journal_mode=%s, attempt %d)%s",jm.c_str(),attempt,attempt?"":"; retrying with exclusive locking"); note(m);
        sqlite3_close(db); db=nullptr;
    }
    note("REGB sqlite UNAVAILABLE: registry is in-memory only this run");
}
// One-time import of the old registry.json into an empty database (PRAGMA user_version 0 -> 1, same transaction).
static void import_legacy_json() {
    int uv=atoi(pragma_text("PRAGMA user_version").c_str());
    if(uv>=1) return;
    bool had=parse_json_file();
    Txn t; if(!t.open) { keys.clear(); return; }
    size_t nk=0,nv=0;
    if(had) for(auto& kv:keys) {
        if(!put_key_row(kv.first,kv.second.path)) { note("REGB import failed; will retry next run"); keys.clear(); return; } nk++;
        for(auto& vv:kv.second.values) { if(!put_value_row(kv.first,kv.second.path,vv.second)) { note("REGB import failed; will retry next run"); keys.clear(); return; } nv++; }
    }
    sql("PRAGMA user_version=1");
    bool done=t.commit(); keys.clear();
    if(!done) { note("REGB import commit failed; will retry next run"); return; }
    if(had) {
        char m[200]; snprintf(m,sizeof m,"REGB imported registry.json: %u keys, %u values -> registry.sqlite",(unsigned)nk,(unsigned)nv); note(m);
        std::wstring dst=file+L".imported"; if(!MoveFileExW(file.c_str(),dst.c_str(),MOVEFILE_REPLACE_EXISTING)) note("REGB import: could not rename registry.json (harmless: user_version already 1)");
    }
}
static void load_db() {
    keys.clear(); if(!db_ok) return;
    sqlite3_stmt* st=nullptr;
    if(sqlite3_prepare_v2(db,"SELECT key_lc,key_path,is_val,val_name,type,data FROM reg ORDER BY is_val",-1,&st,nullptr)!=SQLITE_OK) { note("REGB load: select failed"); return; }
    while(sqlite3_step(st)==SQLITE_ROW) {
        std::wstring lk=wide((const char*)sqlite3_column_text(st,0)); std::wstring path=wide((const char*)sqlite3_column_text(st,1));
        Key& k=keys[lk]; if(k.path.empty()) k.path=path;
        if(sqlite3_column_int(st,2)) {
            Value v; const unsigned char* n=sqlite3_column_text(st,3); v.name=n?wide((const char*)n):L"";
            v.type=(DWORD)sqlite3_column_int64(st,4); const BYTE* b=(const BYTE*)sqlite3_column_blob(st,5); int nb=sqlite3_column_bytes(st,5);
            if(b&&nb>0) v.data.assign(b,b+nb);
            k.values[lower(v.name)]=v;
        }
    }
    sqlite3_finalize(st);
}
static void load_file() { loaded=true; keys.clear(); open_db(); if(db_ok) { import_legacy_json(); load_db(); } }
// Call before any guest runs. dir = LocalState folder.
static void init(const std::wstring& dir, void (*log)(const char*)) {
    std::lock_guard<std::mutex> g(lock); file=dir+L"\\registry.json"; dbfile=dir+L"\\registry.sqlite"; logger=log; loaded=false; handles.clear();
    if(db) { sqlite3_close(db); db=nullptr; } db_ok=false;   // a re-init (tests) reopens and reloads from disk
}
static void ensure() { if(!loaded) load_file(); }

// ---- Win32 semantics
static bool resolve_parent(HKEY h,std::wstring& base) {  // base = lowercase full path of an open key or root
    uintptr_t v=(uintptr_t)h;
    if(auto r=root_name(v&0xFFFFFFFFu)) { if((v>>32)==0||(v>>32)==0xFFFFFFFFu) { base=lower(wide(r)); return true; } }
    auto it=handles.find(v); if(it==handles.end()) return false;
    base=it->second; return true;
}
static std::wstring display_base(const std::wstring& lowerbase) { // original-case path for an existing key or root
    auto it=keys.find(lowerbase); if(it!=keys.end()) return it->second.path;
    std::wstring r=lowerbase; for(auto& c:r) c=(wchar_t)towupper(c); return r;
}
static bool is_root(const std::wstring& l) { return l==L"hkcr"||l==L"hkcu"||l==L"hklm"||l==L"hku"; }
static bool exists(const std::wstring& l) { return is_root(l) || keys.count(l); }
static LONG split(LPCWSTR sub,std::wstring& out) { // validates and normalizes a subkey ("" ok)
    out.clear(); if(!sub) return 0;
    std::wstring s=sub; if(!s.empty() && s[0]==L'\\') return ERROR_BAD_PATHNAME;
    if(s.size()>255*16) return ERROR_INVALID_PARAMETER;
    std::wstring part;
    for(size_t i=0;i<=s.size();i++) {
        if(i==s.size()||s[i]==L'\\') { if(!part.empty()) { if(!out.empty()) out+=L'\\'; out+=part; part.clear(); } }
        else part+=s[i];
    }
    return 0;
}
static HKEY new_handle(const std::wstring& l) { uintptr_t h=next_handle; next_handle+=4; handles[h]=l; return (HKEY)h; }
static LONG create(HKEY parent,LPCWSTR sub,PHKEY out,DWORD* disp) {
    std::lock_guard<std::mutex> g(lock); ensure();
    if(!out) return ERROR_INVALID_PARAMETER;
    std::wstring base; if(!resolve_parent(parent,base)) return ERROR_INVALID_HANDLE;
    std::wstring rel; if(LONG e=split(sub,rel)) return e;
    std::wstring full=display_base(base), lfull=base; bool created=false;
    std::vector<std::pair<std::wstring,std::wstring>> newkeys;
    size_t pos=0;
    while(pos<rel.size()) {
        size_t e=rel.find(L'\\',pos); std::wstring comp=rel.substr(pos,e==std::wstring::npos?std::wstring::npos:e-pos);
        pos=e==std::wstring::npos?rel.size():e+1;
        full+=L"\\"+comp; lfull+=L"\\"+lower(comp);
        if(!keys.count(lfull)) { Key k; k.path=full; keys[lfull]=k; created=true; newkeys.push_back({lfull,full}); }
    }
    if(created) db_put_keys(newkeys);   // all new path components in ONE transaction
    if(disp) *disp=created?REG_CREATED_NEW_KEY:REG_OPENED_EXISTING_KEY;
    *out=new_handle(lfull); return ERROR_SUCCESS;
}
static LONG WINAPI RegCreateKeyW(HKEY h,LPCWSTR sub,PHKEY out) { return create(h,sub,out,nullptr); }
static LONG WINAPI RegCreateKeyExW(HKEY h,LPCWSTR sub,DWORD,LPWSTR,DWORD,REGSAM,const SECURITY_ATTRIBUTES*,PHKEY out,LPDWORD disp) { return create(h,sub,out,disp); }
static LONG WINAPI RegOpenKeyExW(HKEY h,LPCWSTR sub,DWORD,REGSAM,PHKEY out) {
    std::lock_guard<std::mutex> g(lock); ensure();
    if(!out) return ERROR_INVALID_PARAMETER;
    std::wstring base; if(!resolve_parent(h,base)) return ERROR_INVALID_HANDLE;
    std::wstring rel; if(LONG e=split(sub,rel)) return e;
    std::wstring l=base; if(!rel.empty()) l+=L"\\"+lower(rel);
    if(!exists(l)) return ERROR_FILE_NOT_FOUND;
    *out=new_handle(l); return ERROR_SUCCESS;
}
static LONG WINAPI RegCloseKey(HKEY h) {
    std::lock_guard<std::mutex> g(lock);
    uintptr_t v=(uintptr_t)h;
    if(root_name(v&0xFFFFFFFFu) && ((v>>32)==0||(v>>32)==0xFFFFFFFFu)) return ERROR_SUCCESS;
    return handles.erase(v) ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}
static Key* open_key(HKEY h) {
    std::wstring base; if(!resolve_parent(h,base)) return nullptr;
    auto it=keys.find(base); if(it!=keys.end()) return &it->second;
    if(is_root(base)) { Key k; k.path=display_base(base); return &(keys[base]=k); }  // roots hold values lazily
    return nullptr;
}
static bool is_string(DWORD t) { return t==REG_SZ||t==REG_EXPAND_SZ||t==REG_MULTI_SZ; }
static LONG set_value(HKEY h,const std::wstring& name,DWORD type,const BYTE* data,DWORD cb) {
    if(!data && cb) return ERROR_NOACCESS;
    Key* k=open_key(h); if(!k) return ERROR_INVALID_HANDLE;
    Value v; v.name=name; v.type=type; if(cb) v.data.assign(data,data+cb);
    k->values[lower(name)]=v; db_put_value(lower(k->path),*k,v); return ERROR_SUCCESS;
}
static LONG WINAPI RegSetValueExW(HKEY h,LPCWSTR name,DWORD,DWORD type,const BYTE* data,DWORD cb) {
    std::lock_guard<std::mutex> g(lock); ensure();
    return set_value(h,name?name:L"",type,data,cb);
}
static LONG WINAPI RegSetValueExA(HKEY h,LPCSTR name,DWORD,DWORD type,const BYTE* data,DWORD cb) {
    std::lock_guard<std::mutex> g(lock); ensure();
    std::wstring n; if(name&&*name) { int c=MultiByteToWideChar(CP_ACP,0,name,-1,nullptr,0); if(c>0) { n.resize(c); MultiByteToWideChar(CP_ACP,0,name,-1,&n[0],c); n.resize(c-1); } }
    if(is_string(type) && data && cb) {  // registry stores UTF-16; ANSI strings are converted
        int c=MultiByteToWideChar(CP_ACP,0,(LPCSTR)data,(int)cb,nullptr,0);
        std::vector<WCHAR> w(c>0?c:0); if(c>0) MultiByteToWideChar(CP_ACP,0,(LPCSTR)data,(int)cb,w.data(),c);
        return set_value(h,n,type,(const BYTE*)w.data(),(DWORD)w.size()*2);
    }
    return set_value(h,n,type,data,cb);
}
static LONG query(HKEY h,const std::wstring& name,LPDWORD type,BYTE* data,LPDWORD cb,bool ansi) {
    Key* k=open_key(h); if(!k) return ERROR_INVALID_HANDLE;
    if(data && !cb) return ERROR_INVALID_PARAMETER;
    auto it=k->values.find(lower(name)); if(it==k->values.end()) return ERROR_FILE_NOT_FOUND;
    const Value& v=it->second;
    std::vector<BYTE> out=v.data;
    if(ansi && is_string(v.type) && !v.data.empty()) {
        int c=WideCharToMultiByte(CP_ACP,0,(LPCWSTR)v.data.data(),(int)(v.data.size()/2),nullptr,0,nullptr,nullptr);
        out.assign(c>0?c:0,0); if(c>0) WideCharToMultiByte(CP_ACP,0,(LPCWSTR)v.data.data(),(int)(v.data.size()/2),(LPSTR)out.data(),c,nullptr,nullptr);
    }
    if(type) *type=v.type;
    if(!data) { if(cb) *cb=(DWORD)out.size(); return ERROR_SUCCESS; }
    if(*cb<out.size()) { *cb=(DWORD)out.size(); return ERROR_MORE_DATA; }
    if(!out.empty()) memcpy(data,out.data(),out.size());
    *cb=(DWORD)out.size(); return ERROR_SUCCESS;
}
static LONG WINAPI RegQueryValueExW(HKEY h,LPCWSTR name,LPDWORD,LPDWORD type,LPBYTE data,LPDWORD cb) {
    std::lock_guard<std::mutex> g(lock); ensure(); return query(h,name?name:L"",type,data,cb,false);
}
static LONG WINAPI RegQueryValueExA(HKEY h,LPCSTR name,LPDWORD,LPDWORD type,LPBYTE data,LPDWORD cb) {
    std::lock_guard<std::mutex> g(lock); ensure();
    std::wstring n; if(name&&*name) { int c=MultiByteToWideChar(CP_ACP,0,name,-1,nullptr,0); if(c>0) { n.resize(c); MultiByteToWideChar(CP_ACP,0,name,-1,&n[0],c); n.resize(c-1); } }
    return query(h,n,type,data,cb,true);
}
static LONG WINAPI RegDeleteValueA(HKEY h,LPCSTR name) {
    std::lock_guard<std::mutex> g(lock); ensure();
    Key* k=open_key(h); if(!k) return ERROR_INVALID_HANDLE;
    std::wstring n; if(name&&*name) { int c=MultiByteToWideChar(CP_ACP,0,name,-1,nullptr,0); if(c>0) { n.resize(c); MultiByteToWideChar(CP_ACP,0,name,-1,&n[0],c); n.resize(c-1); } }
    if(!k->values.erase(lower(n))) return ERROR_FILE_NOT_FOUND;
    db_del_value(lower(k->path),lower(n)); return ERROR_SUCCESS;
}
struct Export { const char* name; uintptr_t address; };
static const Export exports[]={
    {"RegCreateKeyW",(uintptr_t)&RegCreateKeyW},{"RegCreateKeyExW",(uintptr_t)&RegCreateKeyExW},
    {"RegSetValueExA",(uintptr_t)&RegSetValueExA},{"RegSetValueExW",(uintptr_t)&RegSetValueExW},
    {"RegQueryValueExA",(uintptr_t)&RegQueryValueExA},{"RegQueryValueExW",(uintptr_t)&RegQueryValueExW},
    {"RegDeleteValueA",(uintptr_t)&RegDeleteValueA},{"RegOpenKeyExW",(uintptr_t)&RegOpenKeyExW},{"RegCloseKey",(uintptr_t)&RegCloseKey},
};
static uintptr_t lookup(const char* name) { for(auto& e:exports) if(!strcmp(e.name,name)) return e.address; return 0; }
}
