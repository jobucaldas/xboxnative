// Read-only PE32 inspection on the real Play path. No native import addresses
// are truncated into an i386 IAT. The relocated image remains data, never RX.
#pragma once
namespace pe32_probe {
static bool map(const std::wstring& path, const std::wstring& root, std::string& summary) {
    try {
        FILE* f=_wfopen(path.c_str(),L"rb"); if(!f) throw std::runtime_error("PE32 open failed");
        _fseeki64(f,0,SEEK_END); auto length=_ftelli64(f); rewind(f);
        if(length<0 || length>256*1024*1024) { fclose(f); throw std::runtime_error("PE32 file size"); }
        std::vector<uint8_t> file((size_t)length); auto got=fread(file.data(),1,file.size(),f); fclose(f);
        if(got!=file.size()) throw std::runtime_error("PE32 short read");
        wxl::Pe32 image; image.map(file);
        unsigned gap=0, unknown=0;
        // Match wine_gap's policy: supplied game modules are not missing.
        std::string csv="machine,dll,symbol,console_route,win32u\n";
        for(auto& pair:image.imports) {
            std::string route="unclassified";
            for(auto& r:wine_gap::routes) if(pair.first==r.dll) { route=r.kind; break; }
            std::wstring dll(pair.first.begin(),pair.first.end());
            if(file_exists(root+L"\\"+dll)) route="game";
            if(route!="system" && route!="game") ++gap;
            bool found=false, win32u=false;
            for(auto& row:wine_gap::catalog) if(pair.first==row.dll && pair.second==row.symbol) {
                found=true; win32u=row.win32u; break;
            }
            if(!found) ++unknown;
            csv+="i386,\""+pair.first+"\",\""+pair.second+"\","+route+","+(found?(win32u?"1":"0"):"unknown")+"\n";
        }
        write_text(g_dir+L"\\pe32-imports-"+std::to_wstring(g_launch.appid.load())+L".csv",csv);
        char b[256]; snprintf(b,sizeof b,"PE32 mapped, imports=%zu, gap=%u; relocations=%u reloc-errors=0; logical-base=0x%08x; NO execution",
            image.imports.size(),gap,image.relocations,image.logical); summary=b; lp_sink(b);
        snprintf(b,sizeof b,"PE32 imports saved per-game pe32-imports-<appid>.csv; Wine heuristic unknown=%u; IAT unbound; data-only mapping",unknown); lp_sink(b);
        return true;
    } catch(const std::exception& e) { lp_sink((std::string("PE32 FAIL ")+e.what()).c_str()); return false; }
}
}
