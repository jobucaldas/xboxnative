// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device contracts of the D3D11 failure diagnostics (host/d3d11_diag.h) on a
// native Wine d3d11 device: failed calls log once per distinct call, results
// pass through unchanged, several devices and d3d11.1 interfaces are safe, the
// log is bounded, and the draw summary counts what a context submits. No
// rendering claim.
// Two processes: `d3d11-diag.exe` covers the failed-call log, `d3d11-diag.exe
// draw` the draw summary. Wine 11.0 faults at process teardown when one process
// both exercises the failure hooks and draws on a real device (the unpatched
// header does the same); each half alone exits cleanly.
#include <d3d11_1.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../host/d3d11_diag.h"
static unsigned checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL line %d: %s\n",__LINE__,#x);} } while(0)
static std::vector<std::string> lines;
static void sinkLine(const char* text){lines.push_back(text);}
static size_t count(const char* needle){size_t n=0;for(auto& l:lines)if(l.find(needle)!=std::string::npos)++n;return n;}
static HRESULT STDMETHODCALLTYPE alwaysInvalid(void*,void*,void*,void*){return E_INVALIDARG;}
static ID3D11Device* make(ID3D11DeviceContext** context=nullptr) {
    static const D3D_DRIVER_TYPE types[]={D3D_DRIVER_TYPE_HARDWARE,D3D_DRIVER_TYPE_WARP,D3D_DRIVER_TYPE_REFERENCE};
    for(auto type:types) {
        ID3D11Device* device=nullptr;
        if(SUCCEEDED(D3D11CreateDevice(nullptr,type,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,context)))return device;
    }
    return nullptr;
}
// The draw summary on the real immediate context: the counters follow the
// topology and shaders set on that context, and a render target set is
// described once by format and size. Results are untouched (draws with no
// shaders bound are legal; they just produce nothing).
static int drawSummary(ID3D11Device* device,ID3D11DeviceContext* context) {
    d3d11_diag::configure(sinkLine);
    d3d11_diag::adopt(device);
    CHECK(count("diagnostics on")==1);
    D3D11_TEXTURE2D_DESC target{};target.Width=64;target.Height=64;target.MipLevels=1;target.ArraySize=1;
    target.Format=DXGI_FORMAT_R8G8B8A8_UNORM;target.SampleDesc.Count=1;target.Usage=D3D11_USAGE_DEFAULT;target.BindFlags=D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* targetTexture=nullptr;ID3D11RenderTargetView* rtv=nullptr;
    CHECK(SUCCEEDED(device->CreateTexture2D(&target,nullptr,&targetTexture)));
    CHECK(SUCCEEDED(device->CreateRenderTargetView(targetTexture,nullptr,&rtv)));
    context->OMSetRenderTargets(1,&rtv,nullptr);
    context->OMSetRenderTargets(1,&rtv,nullptr);   // same view: described once
    CHECK(count("draw summary: first render target fmt=28(R8G8B8A8_UNORM) 64x64")==1);
    CHECK(d3d11_diag::mainWidth.load()==64&&d3d11_diag::mainHeight.load()==64);
    const FLOAT black[4]={0,0,0,1};
    context->ClearRenderTargetView(rtv,black);
    CHECK(d3d11_diag::clears[0].load()>=1);
    // ID3D11DeviceContext1::ClearView is the 11.1 way to clear the same view:
    // a game that only clears through it must not show clears=0.
    ID3D11DeviceContext1* context1=nullptr;
    if(SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&context1)))&&context1) {
        // The hook must occupy the ClearView slot itself. The counter alone
        // cannot prove that: Wine's ClearView may reach ClearRenderTargetView
        // internally, which would move clears[0] even with no hook installed.
        void* saved=d3d11_diag::original(context1,d3d11_diag::Context1ClearView);
        void** vtable=*reinterpret_cast<void***>(context1);
        size_t patchCount=0;const d3d11_diag::Patch* patches=d3d11_diag::context1Patches(patchCount);
        CHECK(saved!=nullptr);
        CHECK(patchCount==1&&patches[0].slot==d3d11_diag::Context1ClearView);
        CHECK(vtable[d3d11_diag::Context1ClearView]==patches[0].hook);
        CHECK(saved!=patches[0].hook);
        uint64_t beforeView=d3d11_diag::clears[0].load();
        context1->ClearView(rtv,black,nullptr,0);
        CHECK(d3d11_diag::clears[0].load()==beforeView+1);
        // A depth view cannot be reached through ClearRenderTargetView, so
        // this counts only if the hook body itself ran and classified it.
        D3D11_TEXTURE2D_DESC depth{};depth.Width=64;depth.Height=64;depth.MipLevels=1;depth.ArraySize=1;
        depth.Format=DXGI_FORMAT_D32_FLOAT;depth.SampleDesc.Count=1;depth.Usage=D3D11_USAGE_DEFAULT;depth.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        ID3D11Texture2D* depthTexture=nullptr;ID3D11DepthStencilView* dsv=nullptr;
        if(SUCCEEDED(device->CreateTexture2D(&depth,nullptr,&depthTexture))&&SUCCEEDED(device->CreateDepthStencilView(depthTexture,nullptr,&dsv))) {
            uint64_t beforeDepth=d3d11_diag::clears[1].load();
            context1->ClearView(dsv,black,nullptr,0);
            CHECK(d3d11_diag::clears[1].load()==beforeDepth+1);
        }
        if(dsv)dsv->Release();
        if(depthTexture)depthTexture->Release();
        context1->Release();
    }
    uint64_t beforeIndexed=d3d11_diag::draws[0][0][0].load(),beforeTriangle=d3d11_diag::draws[1][0][0].load();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->Draw(3,0);
    CHECK(d3d11_diag::draws[1][0][0].load()==beforeTriangle+1);
    CHECK(count("draw summary: first Draw topology=4(trianglelist) hs/ds=none")==1);
    context->DrawIndexed(3,0,0);
    CHECK(d3d11_diag::draws[0][0][0].load()==beforeIndexed+1);
    // A patch list with no hull shader bound is still a patch-list draw.
    uint64_t beforePatch=d3d11_diag::draws[0][1][0].load();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    context->DrawIndexed(3,0,0);
    CHECK(d3d11_diag::draws[0][1][0].load()==beforePatch+1);
    CHECK(count("draw summary: first DrawIndexed topology=35(patchlist-3) hs/ds=none")==1);
    // Zero counts are counted separately.
    uint64_t beforeZero=d3d11_diag::zeroCounts[2].load();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->DrawIndexedInstanced(0,1,0,0,0);
    CHECK(d3d11_diag::zeroCounts[2].load()==beforeZero+1);
    context->DrawInstanced(3,0,0,0);   // zero instances
    CHECK(d3d11_diag::zeroCounts[3].load()>=1);
    // Submissions whose counts live on the GPU: the terrain layer of a
    // tessellating game goes through these, and the direct-draw counters
    // cannot see them. Zeroed argument buffers draw nothing, so the calls are
    // legal on any device.
    D3D11_BUFFER_DESC argsDesc{};argsDesc.ByteWidth=64;argsDesc.Usage=D3D11_USAGE_DEFAULT;
    argsDesc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;argsDesc.MiscFlags=D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    D3D11_SUBRESOURCE_DATA zeroArgs{};unsigned char args[64]{};
    argsDesc.ByteWidth=sizeof args;zeroArgs.pSysMem=args;
    ID3D11Buffer* argsBuffer=nullptr;
    CHECK(SUCCEEDED(device->CreateBuffer(&argsDesc,&zeroArgs,&argsBuffer)));
    // The arguments of an indirect draw are uploaded, not passed: the counts
    // the guest supplied are logged so a zero-count submission is visible.
    CHECK(count("indirect args buffer bytes=64")==1);
    CHECK(count("index=0 instances=0 startIndex=0 baseVertex=0 startInstance=0")==1);
    // A tightly sized argument buffer is read only as far as it goes: a
    // 16-byte DrawInstancedIndirect upload must not be read as 20 bytes.
    D3D11_BUFFER_DESC shortDesc=argsDesc;shortDesc.ByteWidth=16;
    UINT shortArgs[4]={5,1,0,0};D3D11_SUBRESOURCE_DATA shortInit{};shortInit.pSysMem=shortArgs;
    ID3D11Buffer* shortBuffer=nullptr;
    CHECK(SUCCEEDED(device->CreateBuffer(&shortDesc,&shortInit,&shortBuffer)));
    CHECK(count("indirect args buffer bytes=16")==1);
    CHECK(count("words=5,1,0,0 (16 bytes)")==1);
    if(argsBuffer) {
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
        uint64_t beforeAuto=d3d11_diag::draws[d3d11_diag::kDrawAuto][1][0].load();
        context->DrawAuto();
        CHECK(d3d11_diag::draws[d3d11_diag::kDrawAuto][1][0].load()==beforeAuto+1);
        CHECK(count("draw summary: first DrawAuto topology=35(patchlist-3) hs/ds=none")==1);
        uint64_t beforeIndexedIndirect=d3d11_diag::draws[d3d11_diag::kDrawIndexedIndirect][1][0].load();
        context->DrawIndexedInstancedIndirect(argsBuffer,0);
        CHECK(d3d11_diag::draws[d3d11_diag::kDrawIndexedIndirect][1][0].load()==beforeIndexedIndirect+1);
        uint64_t beforeInstancedIndirect=d3d11_diag::draws[d3d11_diag::kDrawInstancedIndirect][1][0].load();
        context->DrawInstancedIndirect(argsBuffer,0);
        CHECK(d3d11_diag::draws[d3d11_diag::kDrawInstancedIndirect][1][0].load()==beforeInstancedIndirect+1);
        CHECK(count("draw summary: first DrawIndexedInstancedIndirect topology=35(patchlist-3) hs/ds=none")==1);
        CHECK(count("draw summary: first DrawInstancedIndirect topology=35(patchlist-3) hs/ds=none")==1);
        uint64_t beforeDispatchIndirect=d3d11_diag::dispatchIndirects.load();
        context->DispatchIndirect(argsBuffer,0);
        CHECK(d3d11_diag::dispatchIndirects.load()==beforeDispatchIndirect+1);
        // A later upload of the same buffer is read where the guest writes it.
        UINT newArgs[5]={36,1,0,0,0};
        context->UpdateSubresource(argsBuffer,0,nullptr,newArgs,0,0);
        CHECK(count("indirect args upload count=36 instances=1 startIndex=0 baseVertex=0 startInstance=0")==1);
        // A 12-byte DispatchIndirect upload is read as three words, not five.
        D3D11_BUFFER_DESC dispatchDesc=argsDesc;dispatchDesc.ByteWidth=12;
        UINT groups[3]={8,4,1};D3D11_SUBRESOURCE_DATA dispatchInit{};dispatchInit.pSysMem=groups;
        ID3D11Buffer* dispatchBuffer=nullptr;
        if(SUCCEEDED(device->CreateBuffer(&dispatchDesc,&dispatchInit,&dispatchBuffer))&&dispatchBuffer) {
            UINT newGroups[3]={16,8,1};
            context->UpdateSubresource(dispatchBuffer,0,nullptr,newGroups,0,0);
            CHECK(count("indirect args upload words=16,8,1 (12 bytes)")==1);
            dispatchBuffer->Release();
        }
        if(shortBuffer)shortBuffer->Release();
        // Bound together with UAVs, and sampled with a null entry: the two
        // shapes a layer that renders off-screen and samples what it wrote has.
        D3D11_TEXTURE2D_DESC uavTextureDesc=target;uavTextureDesc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        ID3D11Texture2D* uavTexture=nullptr;ID3D11UnorderedAccessView* uav=nullptr;
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};uavDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        uavDesc.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
        CHECK(SUCCEEDED(device->CreateTexture2D(&uavTextureDesc,nullptr,&uavTexture)));
        CHECK(SUCCEEDED(device->CreateUnorderedAccessView(uavTexture,&uavDesc,&uav)));
        if(uav) {
            uint64_t beforeUavSets=d3d11_diag::uavSets.load();
            context->OMSetRenderTargetsAndUnorderedAccessViews(1,&rtv,nullptr,1,1,&uav,nullptr);
            CHECK(d3d11_diag::rtvUavSets.load()>=1);
            CHECK(d3d11_diag::uavSets.load()==beforeUavSets+1);
            // D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL is a sentinel, not a
            // view count: binding a UAV while keeping the targets must not walk
            // the view array 0xffffffff times, and must leave the format alone.
            unsigned beforeKeep=d3d11_diag::stateOf(context).rtFormat.load();
            context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,1,1,&uav,nullptr);
            CHECK(d3d11_diag::stateOf(context).rtFormat.load()==beforeKeep);
            CHECK(d3d11_diag::uavSets.load()==beforeUavSets+2);
            uav->Release();
        }
        if(uavTexture)uavTexture->Release();
        // Unbinding the targets leaves no format: a later draw must not be
        // bucketed as the target that was bound before.
        context->OMSetRenderTargets(0,nullptr,nullptr);
        CHECK(d3d11_diag::stateOf(context).rtFormat.load()==0);
        uint64_t beforeNoTarget=d3d11_diag::drawsNoTarget.load();
        context->Draw(3,0);
        CHECK(d3d11_diag::drawsNoTarget.load()==beforeNoTarget+1);
        context->OMSetRenderTargets(1,&rtv,nullptr);
        ID3D11ShaderResourceView* nullView=nullptr;
        uint64_t beforeSrvSets=d3d11_diag::srvSets[1].load(),beforeSrvNulls=d3d11_diag::srvNulls[1].load();
        context->PSSetShaderResources(0,1,&nullView);
        CHECK(d3d11_diag::srvSets[1].load()==beforeSrvSets+1);
        CHECK(d3d11_diag::srvNulls[1].load()==beforeSrvNulls+1);
        // A target that reaches the frame by copy/resolve, not by a draw.
        D3D11_TEXTURE2D_DESC copyDesc=target;copyDesc.Width=32;copyDesc.Height=32;
        ID3D11Texture2D* copyTexture=nullptr;
        CHECK(SUCCEEDED(device->CreateTexture2D(&copyDesc,nullptr,&copyTexture)));
        D3D11_TEXTURE2D_DESC msaaDesc=target;msaaDesc.SampleDesc.Count=2;
        ID3D11Texture2D* msaaTexture=nullptr;
        HRESULT msaa=copyTexture?device->CreateTexture2D(&msaaDesc,nullptr,&msaaTexture):E_FAIL;
        uint64_t beforeCopies=d3d11_diag::copies.load(),beforeResolves=d3d11_diag::resolves.load();
        if(copyTexture) {
            context->CopyResource(targetTexture,copyTexture);
            CHECK(d3d11_diag::copies.load()==beforeCopies+1);
        }
        if(SUCCEEDED(msaa)&&msaaTexture) {
            context->ResolveSubresource(targetTexture,0,msaaTexture,0,DXGI_FORMAT_R8G8B8A8_UNORM);
            CHECK(d3d11_diag::resolves.load()==beforeResolves+1);
        }
        if(msaaTexture)msaaTexture->Release();
        if(copyTexture)copyTexture->Release();
        // A draw under a predicate can be skipped entirely by the driver.
        ID3D11Predicate* predicate=nullptr;
        D3D11_QUERY_DESC queryDesc{};queryDesc.Query=D3D11_QUERY_OCCLUSION_PREDICATE;
        if(SUCCEEDED(device->CreatePredicate(&queryDesc,&predicate))&&predicate) {
            uint64_t beforePredicated=d3d11_diag::predicated.load();
            context->SetPredication(predicate,FALSE);
            CHECK(d3d11_diag::predicated.load()==beforePredicated+1);
            context->SetPredication(nullptr,FALSE);
            predicate->Release();
        }
        argsBuffer->Release();
    }
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // A deferred context records with its own topology, and its command list is
    // counted when finished and when executed.
    ID3D11DeviceContext* deferred=nullptr;
    if(SUCCEEDED(device->CreateDeferredContext(0,&deferred))&&deferred) {
        uint64_t beforeDeferred=d3d11_diag::draws[1][0][0].load();
        deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        deferred->Draw(3,0);
        CHECK(d3d11_diag::draws[1][0][0].load()==beforeDeferred+1);
        CHECK(d3d11_diag::deferredContexts.load()>=1);
        ID3D11CommandList* list=nullptr;
        if(SUCCEEDED(deferred->FinishCommandList(FALSE,&list))&&list) {
            CHECK(d3d11_diag::commandLists.load()>=1);
            context->ExecuteCommandList(list,FALSE);
            CHECK(d3d11_diag::executedLists.load()>=1);
            list->Release();
        }
        deferred->Release();
    }
    // The swapchain size the guest asked for is what a summary compares to.
    d3d11_diag::noteSwapChain(1920,1080,DXGI_FORMAT_R8G8B8A8_UNORM,2);
    // Force the next window: the print path is the one the timer uses.
    d3d11_diag::summaryNext.store(0);
    context->Draw(3,0);
    CHECK(count("draw summary 1/7: last topology=4(trianglelist)")==1);
    CHECK(count("draw summary 1/7: Draw calls=")==1);
    CHECK(count("draw summary 1/7: DrawIndexed calls=")==1);
    CHECK(count("draw summary 1/7: DrawIndexedInstanced calls=")==1);
    CHECK(count("draw summary 1/7: DrawInstanced calls=")==1);
    // The GPU-counted kinds are summarised with the same buckets, so a
    // tessellated layer that only submits them is visible.
    CHECK(count("draw summary 1/7: DrawAuto calls=1 patchlist=1 hull-domain=0")==1);
    CHECK(count("draw summary 1/7: DrawIndexedInstancedIndirect calls=1 patchlist=1")==1);
    CHECK(count("draw summary 1/7: DrawInstancedIndirect calls=1 patchlist=1")==1);
    CHECK(count("draw summary 1/7: Dispatch calls=0 indirect=1")==1);
    CHECK(count("draw summary 1/7: targets with UAVs sets=2 uav=2")==1);
    CHECK(count("draw summary 1/7: shader resources vs=0/0 ps=1/1")==1);
    // Draws are bucketed by the format of the target they were submitted to,
    // so a layer that never reaches the swapchain's format is visible.
    CHECK(count("draw summary 1/7: draws by target format 28(R8G8B8A8_UNORM)=")==1);
    // Draws with nothing bound are reported apart from the format buckets: the
    // one here and the deferred context's, which never had a target bound.
    CHECK(count("no-target=2")==1);
    CHECK(count("draw summary 1/7: command lists finish=")==1);
    CHECK(count("draw summary 1/7: shader creates vs=")==1);
    CHECK(count("draw summary 1/7: render targets sets=")==1);
    // The 11.1 ClearView count is reported next to the 11.0 clears.
    CHECK(count("clearview=0")==1);
    CHECK(count("draw summary 1/7: render target fmt=28(R8G8B8A8_UNORM) 64x64")==1);
    CHECK(count("draw summary 1/7: main render target 64x64 swapchain 1920x1080 fmt=28(R8G8B8A8_UNORM) buffers=2")==1);
    // The summary is bounded like the rest of the log.
    size_t summaryLines=0;
    for(auto& l:lines)if(l.find("draw summary")!=std::string::npos)++summaryLines;
    CHECK(summaryLines<=d3d11_diag::kMaxSummaryLines);
    if(rtv)rtv->Release();
    if(targetTexture)targetTexture->Release();
    if(failures)for(auto& l:lines)printf("  %s\n",l.c_str());
    printf("OFF-DEVICE D3D11 draw summary checks=%u failures=%u lines=%zu; no rendering claim\n",checks,failures,lines.size());
    return failures?1:0;
}
int main(int argc,char** argv) {
    bool drawMode=argc>1&&std::string(argv[1])=="draw";
    ID3D11DeviceContext* context=nullptr;
    ID3D11Device* device=make(&context);
    if(!device){printf("OFF-DEVICE no d3d11 device available\n");return 2;}
    if(drawMode)return drawSummary(device,context);
    // A bare DXBC container with a pixel shader chunk (ps_5_0): enough for the
    // model/hash parser; the device rejects it as a shader.
    unsigned char dxbc[64]{};memcpy(dxbc,"DXBC",4);
    uint32_t chunkCount=1,chunkOffset=36;memcpy(dxbc+28,&chunkCount,4);memcpy(dxbc+32,&chunkOffset,4);
    memcpy(dxbc+36,"SHEX",4);uint32_t chunkSize=8,token=0x50;memcpy(dxbc+40,&chunkSize,4);memcpy(dxbc+44,&token,4);
    std::string key,text=d3d11_diag::shaderText(dxbc,sizeof dxbc,&key);
    CHECK(text.find("model=ps_5_0")!=std::string::npos&&key.size()==16);
    CHECK(d3d11_diag::shaderText("junk",4).find("model=not-dxbc")!=std::string::npos);
    CHECK(d3d11_diag::shaderText(nullptr,0)=="bytecode=none");

    // Nothing is logged, and nothing is patched, before the sink is set.
    d3d11_diag::adopt(device);
    CHECK(lines.empty()&&d3d11_diag::classCount.load()==0);
    d3d11_diag::configure(sinkLine);
    d3d11_diag::adopt(device);
    size_t classesAfterFirst=d3d11_diag::classCount.load();
    CHECK(classesAfterFirst>=1&&classesAfterFirst<=3);
    CHECK(count("diagnostics on")==1&&count("device feature level=")==1);

    // Failed creations: same results as the device gives, one line each.
    D3D11_TEXTURE2D_DESC bad{};bad.Width=64;bad.Height=64;bad.MipLevels=1;bad.ArraySize=1;bad.Format=DXGI_FORMAT_UNKNOWN;
    bad.SampleDesc.Count=1;bad.Usage=D3D11_USAGE_DEFAULT;bad.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* texture=nullptr;
    HRESULT hr=device->CreateTexture2D(&bad,nullptr,&texture);
    CHECK(FAILED(hr)&&!texture);
    CHECK(count("CreateTexture2D hr=")==1&&count("fmt=0")==1&&count("64x64")==1);
    bad.Width=128;bad.Height=32;   // same kind of failure at another size: not logged again
    CHECK(FAILED(device->CreateTexture2D(&bad,nullptr,&texture)));
    CHECK(count("CreateTexture2D hr=")==1);
    bad.BindFlags=D3D11_BIND_RENDER_TARGET;   // a different kind of failure
    CHECK(FAILED(device->CreateTexture2D(&bad,nullptr,&texture)));
    CHECK(count("CreateTexture2D hr=")==2);
    // A success is untouched and silent.
    D3D11_TEXTURE2D_DESC good=bad;good.Format=DXGI_FORMAT_R8G8B8A8_UNORM;good.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    size_t before=lines.size();
    CHECK(SUCCEEDED(device->CreateTexture2D(&good,nullptr,&texture))&&texture&&lines.size()==before);

    D3D11_BUFFER_DESC emptyBuffer{};emptyBuffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    ID3D11Buffer* buffer=nullptr;
    CHECK(FAILED(device->CreateBuffer(&emptyBuffer,nullptr,&buffer))&&!buffer&&count("CreateBuffer hr=")==1);
    // Views report the resource they were made on.
    ID3D11ShaderResourceView* srv=nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC badView{};badView.Format=DXGI_FORMAT(0x7fff);badView.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;badView.Texture2D.MipLevels=1;
    CHECK(FAILED(device->CreateShaderResourceView(texture,&badView,&srv))&&!srv);
    CHECK(count("CreateShaderResourceView hr=")==1&&count("res=tex2d fmt=28(R8G8B8A8_UNORM)")==1);
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;
    CHECK(FAILED(device->CreateVertexShader("junk",4,nullptr,&vs))&&!vs&&count("CreateVertexShader hr=")==1);
    CHECK(FAILED(device->CreatePixelShader(dxbc,sizeof dxbc,nullptr,&ps))&&!ps);
    CHECK(count("CreatePixelShader hr=")==1&&count("model=ps_5_0")==1);
    CHECK(FAILED(device->CreatePixelShader(dxbc,sizeof dxbc,nullptr,&ps))&&count("CreatePixelShader hr=")==1);
    ID3D11HullShader* hs=nullptr;ID3D11DomainShader* ds=nullptr;ID3D11ComputeShader* cs=nullptr;ID3D11GeometryShader* gs=nullptr;
    CHECK(FAILED(device->CreateHullShader("junk",4,nullptr,&hs))&&count("CreateHullShader hr=")==1);
    CHECK(FAILED(device->CreateDomainShader("junk",4,nullptr,&ds))&&count("CreateDomainShader hr=")==1);
    CHECK(FAILED(device->CreateComputeShader("junk",4,nullptr,&cs))&&count("CreateComputeShader hr=")==1);
    CHECK(FAILED(device->CreateGeometryShader("junk",4,nullptr,&gs))&&count("CreateGeometryShader hr=")==1);
    D3D11_SAMPLER_DESC sampler{};   // zero address modes are invalid
    ID3D11SamplerState* samplerState=nullptr;
    CHECK(FAILED(device->CreateSamplerState(&sampler,&samplerState))&&count("CreateSamplerState hr=")==1);
    D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    ID3D11InputLayout* layout=nullptr;
    CHECK(FAILED(device->CreateInputLayout(&element,1,"junk",4,&layout))&&count("CreateInputLayout hr=")==1&&count("POSITION0:6@0")==1);
    // Capability answers.
    UINT support=1;
    CHECK(FAILED(device->CheckFormatSupport(DXGI_FORMAT(0x7fff),&support))||support==0);
    CHECK(count("CheckFormatSupport hr=")==1);
    CHECK(SUCCEEDED(device->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM,&support))&&support!=0);
    CHECK(count("CheckFormatSupport fmt=28(R8G8B8A8_UNORM)")==1);
    CHECK(FAILED(device->CheckFeatureSupport(D3D11_FEATURE(9999),&support,sizeof support))&&count("CheckFeatureSupport hr=")==1);
    UINT levels=0;
    HRESULT msaa=device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM,64,&levels);
    CHECK((FAILED(msaa)||levels==0)==(count("CheckMultisampleQualityLevels hr=")==1));

    // Map failure (default-usage buffer is not CPU mappable) on the immediate context.
    D3D11_BUFFER_DESC gpuOnly{};gpuOnly.ByteWidth=64;gpuOnly.BindFlags=D3D11_BIND_VERTEX_BUFFER;gpuOnly.Usage=D3D11_USAGE_DEFAULT;
    CHECK(SUCCEEDED(device->CreateBuffer(&gpuOnly,nullptr,&buffer)));
    D3D11_MAPPED_SUBRESOURCE mapped{};
    CHECK(FAILED(context->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))&&count("Map hr=")==1&&count("res=buffer bytes=64")==1);
    CHECK(FAILED(context->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))&&count("Map hr=")==1);

    // A second device and the d3d11.1 interface share the class: no new patch, same dedupe.
    ID3D11Device* second=make();
    CHECK(second!=nullptr);
    if(second) {
        d3d11_diag::adopt(second);
        CHECK(d3d11_diag::classCount.load()<=classesAfterFirst+2);
        ID3D11Texture2D* again=nullptr;bad.BindFlags=D3D11_BIND_RENDER_TARGET;
        CHECK(FAILED(second->CreateTexture2D(&bad,nullptr,&again)));
        CHECK(count("CreateTexture2D hr=")==2);   // already logged through the first device
        second->Release();
    }

    // Every other hook, against a stand-in whose entries all fail (Wine accepts
    // many invalid state descriptions): installed through the same patch tables.
    {
        static void* vtable[d3d11_diag::kSlots];
        for(auto& entry:vtable)entry=reinterpret_cast<void*>(&alwaysInvalid);
        struct Fake {void** vtable;} fake{vtable};
        size_t baseCount=0,extendedCount=0;
        auto base=d3d11_diag::basePatches(baseCount);auto extended=d3d11_diag::extendedPatches(extendedCount);
        CHECK(d3d11_diag::patchClass(vtable,base,baseCount)&&d3d11_diag::patchClass(vtable,extended,extendedCount));
        CHECK(d3d11_diag::patchClass(vtable,base,baseCount));   // idempotent
        for(size_t i=0;i<baseCount;++i)CHECK(vtable[base[i].slot]==base[i].hook);
        void* self=&fake;
        auto call=[&](int slot,auto... args)->HRESULT{return reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(void*,decltype(args)...)>(vtable[slot])(self,args...);};
        D3D11_BLEND_DESC blend{};blend.RenderTarget[0].BlendEnable=TRUE;
        D3D11_BLEND_DESC1 blend1{};D3D11_DEPTH_STENCIL_DESC depth{};D3D11_RASTERIZER_DESC raster{};D3D11_RASTERIZER_DESC1 raster1{};
        D3D11_QUERY_DESC query{};query.Query=D3D11_QUERY_OCCLUSION;D3D11_TEXTURE1D_DESC t1{};t1.Width=16;D3D11_TEXTURE3D_DESC t3{};t3.Depth=4;
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
        D3D11_SO_DECLARATION_ENTRY so{};
        void* out=nullptr;
        CHECK(call(d3d11_diag::CreateBlendState,&blend,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateBlendState1,&blend1,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateDepthStencilState,&depth,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateRasterizerState,&raster,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateRasterizerState1,&raster1,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateQuery,&query,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreatePredicate,&query,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateTexture1D,&t1,(void*)nullptr,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateTexture3D,&t3,(void*)nullptr,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateUnorderedAccessView,(void*)nullptr,&uav,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateDepthStencilView,(void*)nullptr,&dsv,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateClassLinkage,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateDeferredContext,0u,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateDeferredContext1,0u,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateGeometryShaderWithStreamOutput,"junk",size_t(4),&so,1u,(const UINT*)nullptr,0u,0u,(void*)nullptr,&out)==E_INVALIDARG);
        for(const char* name:{"CreateBlendState hr=","CreateBlendState1 hr=","CreateDepthStencilState hr=","CreateRasterizerState hr=","CreateRasterizerState1 hr=",
                "CreateQuery hr=","CreatePredicate hr=","CreateTexture1D hr=","CreateTexture3D hr=","CreateUnorderedAccessView hr=","CreateDepthStencilView hr=",
                "CreateClassLinkage hr=","CreateDeferredContext hr=","CreateDeferredContext1 hr=","CreateGeometryShaderWithStreamOutput hr="})
            CHECK(count(name)==1);
        CHECK(count("rt0:enable=1")==1&&count("query=")>=2);
    }

    // The log is bounded; one line says so.
    for(UINT i=0;i<400;++i){D3D11_TEXTURE2D_DESC d=bad;d.MiscFlags=0x100000u+i;ID3D11Texture2D* t=nullptr;device->CreateTexture2D(&d,nullptr,&t);}
    CHECK(lines.size()<=d3d11_diag::kMaxFailures+d3d11_diag::kMaxProbes+d3d11_diag::kMaxSummaryLines+4);
    CHECK(count("further failed calls suppressed")==1);
    if(failures)for(auto& l:lines)printf("  %s\n",l.c_str());
    if(texture)texture->Release();
    printf("OFF-DEVICE D3D11 failure diagnostics checks=%u failures=%u lines=%zu; no rendering claim\n",checks,failures,lines.size());
    return failures?1:0;
}
