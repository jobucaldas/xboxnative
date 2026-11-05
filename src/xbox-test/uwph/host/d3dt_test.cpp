// Separate x64 PE: direct system D3D11 import, no rendering in the EXE.
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <winrt/base.h>
#include <stdio.h>
#include "shaders/triangle_bytecode.h"
using winrt::com_ptr;
static com_ptr<ID3D11Device> device;
static com_ptr<ID3D11DeviceContext> context;
static com_ptr<IDXGISwapChain1> swapchain;
static com_ptr<ID3D11RenderTargetView> target;
extern "C" __declspec(dllexport) HRESULT run_d3dt(
 HRESULT (*get_target)(IUnknown**), void (*log)(const char*), void (*pump)()) {
 char msg[256];
 auto report = [&](const char* step, HRESULT hr) {
  snprintf(msg,sizeof(msg),"D3DT %s hr=0x%08lx",step,(unsigned long)hr); log(msg);
  return hr;
 };
 com_ptr<IUnknown> window;
 HRESULT hr=report("get swapchain target",get_target(window.put()));
 if (hr!=S_OK) return hr;
 D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0}, got{};
 hr=report("system D3D11CreateDevice",D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,
  D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,2,D3D11_SDK_VERSION,device.put(),&got,context.put()));
 if (hr!=S_OK) return hr;
 snprintf(msg,sizeof(msg),"D3DT feature_level=0x%x separate x64 PE",(unsigned)got); log(msg);
 com_ptr<IDXGIDevice> dxgi;
 hr=report("QueryInterface IDXGIDevice",device->QueryInterface(__uuidof(IDXGIDevice),dxgi.put_void()));
 if (hr!=S_OK) return hr;
 com_ptr<IDXGIAdapter> adapter;
 hr=report("GetAdapter",dxgi->GetAdapter(adapter.put())); if (hr!=S_OK) return hr;
 com_ptr<IDXGIFactory2> factory;
 hr=report("GetParent IDXGIFactory2",adapter->GetParent(__uuidof(IDXGIFactory2),factory.put_void()));
 if (hr!=S_OK) return hr;
 DXGI_SWAP_CHAIN_DESC1 desc{};
 desc.Width=1920; desc.Height=1080; desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
 desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2;
 desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD; desc.Scaling=DXGI_SCALING_STRETCH;
 desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
 hr=report("system CreateSwapChainForCoreWindow",factory->CreateSwapChainForCoreWindow(device.get(),window.get(),&desc,nullptr,swapchain.put()));
 if (hr!=S_OK) return hr;
 com_ptr<ID3D11Texture2D> buffer;
 hr=report("GetBuffer",swapchain->GetBuffer(0,__uuidof(ID3D11Texture2D),buffer.put_void()));
 if (hr!=S_OK) return hr;
 hr=report("CreateRenderTargetView",device->CreateRenderTargetView(buffer.get(),nullptr,target.put()));
 if (hr!=S_OK) return hr;
 com_ptr<ID3D11VertexShader> vs;
 com_ptr<ID3D11PixelShader> ps;
 com_ptr<ID3D11InputLayout> layout;
 com_ptr<ID3D11Buffer> vertices;
 hr=report("CreateVertexShader DXBC",device->CreateVertexShader(triangle_vs,sizeof(triangle_vs),nullptr,vs.put()));
 if (hr!=S_OK) return hr;
 hr=report("CreatePixelShader DXBC",device->CreatePixelShader(triangle_ps,sizeof(triangle_ps),nullptr,ps.put()));
 if (hr!=S_OK) return hr;
 D3D11_INPUT_ELEMENT_DESC elements[]={
  {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
  {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
 hr=report("CreateInputLayout",device->CreateInputLayout(elements,2,triangle_vs,sizeof(triangle_vs),layout.put()));
 if (hr!=S_OK) return hr;
 // Clockwise front face with distinct corner colors; no runtime compilation.
 const float data[][6]={{0.0f,0.8f,1,0,0,1},{0.8f,-0.8f,0,1,0,1},{-0.8f,-0.8f,0,0,1,1}};
 D3D11_BUFFER_DESC bd{}; bd.ByteWidth=sizeof(data); bd.Usage=D3D11_USAGE_IMMUTABLE; bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
 D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=data;
 hr=report("CreateBuffer triangle",device->CreateBuffer(&bd,&initial,vertices.put()));
 if (hr!=S_OK) return hr;
 D3D11_VIEWPORT viewport{0,0,1920,1080,0,1};
 context->RSSetViewports(1,&viewport);
 context->IASetInputLayout(layout.get());
 UINT stride=sizeof(data[0]),offset=0; ID3D11Buffer* vb=vertices.get();
 context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
 context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 context->VSSetShader(vs.get(),nullptr,0); context->PSSetShader(ps.get(),nullptr,0);
 log("D3DT triangle pipeline ready: precompiled DXBC, Draw(3,0) each frame");
 LARGE_INTEGER start, end, frequency; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
 for (unsigned frame=0;frame<300;++frame) {
  pump();
  // Every frame differs; final clear is bright magenta (R=.9, G=.12, B=.65).
  const float phase=float(frame)/299.0f;
  const float color[]={0.1f+0.8f*phase,0.12f,0.95f-0.3f*phase,1.0f};
  context->ClearRenderTargetView(target.get(),color);
  ID3D11RenderTargetView* rtv=target.get();
  context->OMSetRenderTargets(1,&rtv,nullptr);
  context->Draw(3,0);
  hr=swapchain->Present(1,0);
  if (hr!=S_OK) return report("Present FAILED/nonzero status",hr);
  if ((frame+1)%60==0) {
   QueryPerformanceCounter(&end);
   double seconds=double(end.QuadPart-start.QuadPart)/double(frequency.QuadPart);
   snprintf(msg,sizeof(msg),"D3DT presented=%u hr=0x00000000 elapsed=%.3f fps=%.2f",frame+1,seconds,(frame+1)/seconds); log(msg);
  }
 }
 log("D3DT RESULT presented=300 hr=0x00000000 final_clear=0.90,0.12,0.65,1.00");
 return S_OK;
}
