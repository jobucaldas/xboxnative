#include "../../../xbox-test/app/host/wxl_pe32.h"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc,char** argv) {
    try {
        if(argc!=2 && argc!=3) throw std::runtime_error("usage: pe32-map <PE32> [mapped-data-output]");
        std::ifstream f(argv[1],std::ios::binary);
        if(!f) throw std::runtime_error("open failed");
        std::vector<uint8_t> file{std::istreambuf_iterator<char>(f),{}};
        wxl::Pe32 image; image.map(file);
        if(argc==3) {
            std::ofstream out(argv[2],std::ios::binary);
            out.write(reinterpret_cast<const char*>(image.bytes.data()),image.bytes.size());
            if(!out) throw std::runtime_error("mapped data write failed");
        }
        for(auto& p:image.imports) std::cout<<p.first<<","<<p.second<<"\n";
        std::cerr<<"PE32 mapped, imports="<<image.imports.size()<<", relocations="<<image.relocations<<", reloc-errors=0; no execution\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}
