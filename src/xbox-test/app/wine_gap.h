// Static Wine diagnostic only. Reads PE32/PE32+ imports without mapping or execution.
#pragma once
#include <array>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include "host/x86/x86_play.h"
namespace wine_gap {
struct Meta { const char* dll; const char* symbol; bool win32u; };
static const Meta catalog[] = {
#include "wine_gap_catalog.h"
};
struct Route { const char* dll; const char* kind; };
static const Route routes[] = {
#include "wine_gap_routes.h"
};
struct RouteOverride { const char* dll; const char* symbol; const char* kind; };
static const RouteOverride route_overrides[] = {
#include "wine_gap_overrides.h"
};
struct X86Routes {
    x86::Memory memory;
    x86::Host host;
    x86::Registry registry;
    x86::Guest guest;
    X86Routes() : guest(memory, host, registry) { guest.register_all(); }
};
static bool x86_binds(const x86::Registry& registry, const std::string& dll, const std::string& symbol) {
    const std::string served = x86::apiset::served_by(dll, symbol);
    std::string lookup = symbol;
    if (symbol.size() > 1 && symbol[0] == '#') {
        unsigned ordinal = unsigned(std::stoul(symbol.substr(1)));
        auto it = registry.ordinals.find({served, uint16_t(ordinal)});
        if (it == registry.ordinals.end()) return false;
        lookup = it->second;
    }
    const auto key = std::make_pair(served, x86::Registry::key_sym(lookup));
    return registry.map.count(key) || registry.data.count(key);
}
static std::string route_for_symbol(const std::string& dll, const std::string& symbol, const std::string& fallback) {
    for(const auto& override:route_overrides)
        if(dll==override.dll && symbol==override.symbol) return override.kind;
    return fallback;
}
static bool x86_donor(const std::string& dll) {
    return x86::is_x86_runtime_dll(dll) || x86::is_d3dx9_donor_dll(dll) || x86::apiset::donor_module(dll) != dll;
}
struct Pe {
    std::vector<unsigned char> bytes;
    size_t sections = 0; unsigned count = 0; bool wide = false;
    uint64_t read(size_t off, unsigned n) const {
        if (off > bytes.size() || n > bytes.size() - off) throw std::runtime_error("truncated PE");
        uint64_t v = 0; for (unsigned i=0; i<n; ++i) v |= uint64_t(bytes[off+i]) << (8*i); return v;
    }
    size_t offset(uint32_t rva) const {
        for (unsigned i=0; i<count; ++i) {
            size_t s = sections + 40*i; uint32_t va = (uint32_t)read(s+12,4), raw = (uint32_t)read(s+16,4);
            if (rva >= va && uint64_t(rva)-va < raw) return (size_t)read(s+20,4) + (rva-va);
        }
        // Header RVAs are valid only before the section table.
        if (rva < sections) return rva;
        throw std::runtime_error("invalid PE RVA");
    }
    std::string string(uint32_t rva) const {
        size_t o=offset(rva); std::string s;
        for (unsigned i=0; i<4096; ++i) { char c=(char)read(o+i,1); if (!c) return s; s+=c; }
        throw std::runtime_error("unterminated PE import name");
    }
    void imports(std::set<std::tuple<std::string,std::string,bool>>& pairs) {
        if (read(0,2)!=0x5a4d) throw std::runtime_error("not MZ");
        size_t nt=(size_t)read(0x3c,4); if(read(nt,4)!=0x4550) throw std::runtime_error("not PE");
        count=(unsigned)read(nt+6,2); size_t opt=nt+24; unsigned magic=(unsigned)read(opt,2);
        if(magic!=0x10b && magic!=0x20b) throw std::runtime_error("unsupported PE optional header");
        wide=magic==0x20b; size_t os=(size_t)read(nt+20,2); sections=opt+os;
        if(count>96 || sections>bytes.size() || count*40>bytes.size()-sections) throw std::runtime_error("invalid PE sections");
        size_t dirs=opt+(wide?112:96); unsigned nd=(unsigned)read(opt+(wide?108:92),4);
        for(unsigned index : {1u,13u}) {
            if(nd<=index) continue;
            if(dirs+index*8+8>sections) throw std::runtime_error("invalid PE directories");
            uint32_t rva=(uint32_t)read(dirs+index*8,4), size=(uint32_t)read(dirs+index*8+4,4);
            if(!rva || !size) continue;
            bool delay=index==13; unsigned stride=delay?32:20; bool ended=false;
            for(unsigned i=0; i<size/stride; ++i) {
                if(uint64_t(rva)+i*stride>UINT32_MAX) throw std::runtime_error("PE directory overflow");
                size_t d=offset(rva+i*stride); uint32_t name=(uint32_t)read(d+(delay?4:12),4);
                if(!name) { ended=true; break; }
                if(delay && !(read(d,4)&1)) throw std::runtime_error("VA-based delay imports unsupported");
                std::string dll=string(name); for(auto& c:dll) if(c>='A'&&c<='Z') c+=32;
                uint32_t thunk=(uint32_t)read(d+(delay?16:0),4);
                if(!thunk) thunk=(uint32_t)read(d+(delay?12:16),4);
                bool terminated=false; unsigned width=wide?8:4;
                for(unsigned j=0;j<65536;++j) {
                    uint64_t tr=uint64_t(thunk)+j*width; if(tr>UINT32_MAX) throw std::runtime_error("PE thunk overflow");
                    uint64_t t=read(offset((uint32_t)tr),width); if(!t) { terminated=true; break; }
                    std::string sym;
                    if(t & (wide?(1ull<<63):(1ull<<31))) sym="#"+std::to_string(t&0xffff);
                    else { if(t>UINT32_MAX-2) throw std::runtime_error("PE name overflow"); sym=string((uint32_t)t+2); }
                    pairs.emplace(dll,sym,!wide);
                }
                if(!terminated) throw std::runtime_error("unterminated PE thunk table");
            }
            if(!ended) throw std::runtime_error("unterminated PE import directory");
        }
    }
};
static void report(const std::wstring& root) {
    std::vector<std::wstring> files; std::set<std::string> assets;
    WIN32_FIND_DATAW fd; HANDLE h=FindFirstFileW((root+L"\\*").c_str(),&fd);
    if(h==INVALID_HANDLE_VALUE) { lp_sink("GAP-ERROR: cannot enumerate game directory"); return; }
    do {
        if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring n=fd.cFileName, lower=n; for(auto& c:lower) c=(wchar_t)towlower(c);
        if(lower.size()<4 || (lower.substr(lower.size()-4)!=L".exe" && lower.substr(lower.size()-4)!=L".dll")) continue;
        files.push_back(n); assets.emplace(lower.begin(),lower.end());
    } while(FindNextFileW(h,&fd)); FindClose(h);
    try {
        if(files.empty()) throw std::runtime_error("no top-level PE files");
        std::set<std::tuple<std::string,std::string,bool>> pairs;
        for(auto& n:files) {
            FILE* f=_wfopen((root+L"\\"+n).c_str(),L"rb"); if(!f) throw std::runtime_error("cannot open PE");
            if(_fseeki64(f,0,SEEK_END)) { fclose(f); throw std::runtime_error("cannot size PE"); }
            auto size=_ftelli64(f); rewind(f);
            if(size<0 || size>512*1024*1024) { fclose(f); throw std::runtime_error("PE exceeds diagnostic size limit"); }
            Pe p; p.bytes.resize((size_t)size); auto got=fread(p.bytes.data(),1,p.bytes.size(),f); fclose(f);
            if(got!=p.bytes.size()) throw std::runtime_error("short PE read"); p.imports(pairs);
        }
        unsigned missing=0, win32u=0, unknown=0;
        std::unique_ptr<X86Routes> x86_routes;
        for(const auto& pair:pairs) if(std::get<2>(pair)) { x86_routes.reset(new X86Routes); break; }
        using Counts = std::array<size_t, 4>;
        std::map<std::string, Counts> by_dll;
        std::vector<std::string> gaps;
        constexpr size_t kGapLimit=64;
        for(auto& pair:pairs) {
            const std::string& dll=std::get<0>(pair); const std::string& symbol=std::get<1>(pair);
            const bool pe32=std::get<2>(pair);
            std::string route="unclassified";
            for(auto& r:routes) if(dll==r.dll) { route=r.kind; break; }
            size_t bucket=0;
            if(pe32) {
                // PE32 runs through x86::Guest::register_all and its API-set
                // resolver. Only that registry, actual game modules, and the
                // interpreter's explicit donor allowlist can bind these imports.
                if((x86_routes && x86_binds(x86_routes->registry,dll,symbol)) || assets.count(dll)) bucket=0;
                else if(x86_donor(dll)) bucket=1;
                else bucket=3;
                if(bucket==3) ++missing;
            } else {
                if(!assets.count(dll)) route=route_for_symbol(dll,symbol,route);
                if(!assets.count(dll) && route!="system") ++missing;
                // The oracle's system, uwph and game routes all have an in-process
                // provider; wine-pe is a donor, while unclassified is truly missing.
                if(assets.count(dll) || route=="system" || route=="uwph") bucket=0;
                else if(route=="wine-pe") bucket=1;
                else if(route=="stub") bucket=2;
                else bucket=3;
            }
            ++by_dll[dll][bucket];
            if((bucket==2 || bucket==3) && gaps.size()<kGapLimit)
                gaps.push_back(std::string(bucket==2?"GAP: stub ":"GAP: missing ")+dll+"!"+symbol);
            bool found=false;
            for(auto& row:catalog) if(dll==row.dll && symbol==row.symbol) { found=true; win32u+=row.win32u; break; }
            if(!found) ++unknown;
        }
        char b[256]; snprintf(b,sizeof b,"GAP: total=%zu missing=%u win32u=%u",pairs.size(),missing,win32u); lp_sink(b);
        for(auto& item:by_dll) {
            snprintf(b,sizeof b,"GAP: dll=%s system=%zu donor=%zu stub=%zu missing=%zu",item.first.c_str(),
                item.second[0],item.second[1],item.second[2],item.second[3]); lp_sink(b);
        }
        for(auto& line:gaps) lp_sink(line.c_str());
        size_t unresolved=0;
        for(auto& item:by_dll) unresolved+=item.second[2]+item.second[3];
        if(unresolved>gaps.size()) {
            snprintf(b,sizeof b,"GAP: missing-or-stubbed truncated after %zu names total=%zu",kGapLimit,unresolved); lp_sink(b);
        }
        snprintf(b,sizeof b,"GAP-METADATA: unknown=%u Wine source heuristic; win32u excludes unknowns; routes are policy, not console export evidence",unknown); lp_sink(b);
    } catch(const std::exception& e) { std::string s="GAP-ERROR: "; s+=e.what(); lp_sink(s.c_str()); }
}
}
