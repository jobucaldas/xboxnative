// PE32 inspection mapper. Storage is ordinary non-executable data; logical guest
// addresses never become host pointers. No IAT host bindings, TLS or entry calls.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace wxl {
struct Pe32 {
    std::vector<uint8_t> bytes;
    std::set<std::pair<std::string,std::string>> imports;
    uint32_t preferred = 0, logical = 0, entry = 0;
    unsigned relocations = 0;
    static bool range(size_t p, size_t n, size_t end) { return p<=end && n<=end-p; }
    static uint32_t read(const std::vector<uint8_t>& b, size_t p, unsigned n=4) {
        if (!range(p,n,b.size())) throw std::runtime_error("PE32 truncated field");
        uint32_t v=0; for(unsigned i=0;i<n;++i) v|=uint32_t(b[p+i])<<(8*i); return v;
    }
    void write(size_t p, uint32_t v, unsigned n=4) {
        if(!range(p,n,bytes.size())) throw std::runtime_error("PE32 write bounds");
        for(unsigned i=0;i<n;++i) bytes[p+i]=uint8_t(v>>(8*i));
    }
    std::string string(uint32_t rva) const {
        if(rva>=bytes.size()) throw std::runtime_error("PE32 string bounds");
        auto p=std::find(bytes.begin()+rva,bytes.end(),uint8_t(0));
        if(p==bytes.end()) throw std::runtime_error("PE32 unterminated string");
        return std::string(bytes.begin()+rva,p);
    }
    void map(const std::vector<uint8_t>& file, uint32_t base=0) {
        bytes.clear(); imports.clear(); relocations=0;
        if(read(file,0,2)!=0x5a4d) throw std::runtime_error("PE32 MZ signature");
        size_t nt=read(file,0x3c), opt=nt+24;
        if(read(file,nt)!=0x4550 || read(file,nt+4,2)!=0x14c || read(file,opt,2)!=0x10b)
            throw std::runtime_error("PE32 requires i386/PE32");
        unsigned ns=read(file,nt+6,2), os=read(file,nt+20,2);
        size_t sections=opt+os;
        uint32_t nd=read(file,opt+92);
        if(os<96 || nd>16 || 96+nd*8>os || !ns || ns>96 || !range(sections,ns*40,file.size()))
            throw std::runtime_error("PE32 header bounds");
        uint32_t size=read(file,opt+56), headers=read(file,opt+60), alignment=read(file,opt+32);
        if(!size || size>512u*1024*1024 || headers>size || headers>file.size() ||
           headers<sections+ns*40 || alignment!=4096 || size%4096)
            throw std::runtime_error("PE32 image layout");
        preferred=read(file,opt+28);
        // A stripped/fixed PE must stay at its preferred logical address.
        if(!base) base=(nd>5 && read(file,opt+96+5*8+4))?0x10000000:preferred;
        logical=base; entry=read(file,opt+16);
        if(uint64_t(base)+size>0x100000000ull || entry>=size) throw std::runtime_error("PE32 address bounds");
        bytes.resize(size); memcpy(bytes.data(),file.data(),headers);
        std::vector<std::pair<size_t,size_t>> spans{{0,headers}};
        for(unsigned i=0;i<ns;++i) {
            size_t s=sections+40*i; uint32_t va=read(file,s+12), raw=read(file,s+16), off=read(file,s+20);
            size_t length=std::max(read(file,s+8),raw);
            if(va%alignment || !length || !range(va,length,size) || !range(off,raw,file.size()))
                throw std::runtime_error("PE32 section bounds");
            for(auto span:spans) if(va<span.second && span.first<size_t(va)+length)
                throw std::runtime_error("PE32 section overlap");
            spans.emplace_back(va,size_t(va)+length);
            if(raw) memcpy(bytes.data()+va,file.data()+off,raw);
        }
        auto directory=[&](unsigned index) {
            if(index>=nd) return std::make_pair(0u,0u);
            uint32_t r=read(file,opt+96+index*8), n=read(file,opt+100+index*8);
            if((!r)!=(!n) || !range(r,n,bytes.size())) throw std::runtime_error("PE32 directory bounds");
            return std::make_pair(r,n);
        };
        auto rd=directory(5); uint32_t delta=base-preferred;
        // Fixed images can be inspected at their preferred logical base only.
        if(delta && !rd.second) throw std::runtime_error("PE32 relocation directory absent");
        for(size_t pos=rd.first,end=pos+rd.second;pos<end;) {
            if(!range(pos,8,end)) throw std::runtime_error("PE32 relocation block bounds");
            uint32_t page=read(bytes,pos), length=read(bytes,pos+4);
            if(length<8 || length%2 || !range(pos,length,end)) throw std::runtime_error("PE32 relocation block length");
            for(size_t p=pos+8;p<pos+length;p+=2) {
                uint32_t word=read(bytes,p,2), type=word>>12;
                if(!type) continue;
                size_t target=size_t(page)+(word&4095); unsigned width=type==3?4:2;
                if(type!=1 && type!=2 && type!=3) throw std::runtime_error("PE32 unsupported relocation type");
                uint32_t v=read(bytes,target,width);
                write(target,v+(type==1?(delta>>16):type==2?(delta&65535):delta),width); ++relocations;
            }
            pos+=length;
        }
        for(unsigned index:{1u,13u}) {
            auto id=directory(index); if(!id.second) continue;
            bool delay=index==13, ended=false; unsigned stride=delay?32:20;
            for(size_t pos=id.first,end=pos+id.second;range(pos,stride,end);pos+=stride) {
                uint32_t name=read(bytes,pos+(delay?4:12));
                if(!name) {
                    for(unsigned j=0;j<stride;j+=4) if(read(bytes,pos+j)) throw std::runtime_error("PE32 partial import terminator");
                    ended=true; break;
                }
                if(delay && read(bytes,pos)!=1) throw std::runtime_error("PE32 VA delay imports unsupported");
                auto dll=string(name); for(auto& c:dll) if(c>='A'&&c<='Z') c+=32;
                size_t src=read(bytes,pos+(delay?16:0)), dst=read(bytes,pos+(delay?12:16));
                if(!dst) throw std::runtime_error("PE32 missing IAT");
                if(!src) src=dst;
                bool terminated=false;
                for(unsigned j=0;j<65536;++j) {
                    uint32_t t=read(bytes,src+size_t(j)*4); read(bytes,dst+size_t(j)*4);
                    if(!t) { terminated=true; break; }
                    if(!(t&0x80000000u) && t>UINT32_MAX-2) throw std::runtime_error("PE32 import name overflow");
                    auto sym=(t&0x80000000u)?"#"+std::to_string(t&65535):string(t+2);
                    imports.emplace(dll,sym);
                }
                if(!terminated) throw std::runtime_error("PE32 unterminated import thunks");
            }
            if(!ended) throw std::runtime_error("PE32 unterminated import directory");
        }
    }
};
}
