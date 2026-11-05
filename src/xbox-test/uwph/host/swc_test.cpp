// Trusted CRT-free mapped PE. All rendering calls execute inside this image.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
static const char* volatile anchor="SWC mapped guest"; // DIR64 relocation exercise
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static DWORD fail(){OutputDebugStringA("SWC GUEST FAIL");return 1;}
extern "C" DWORD WINAPI swc_entry(){
 OutputDebugStringA(anchor);
 WNDCLASSEXW wc={}; wc.cbSize=sizeof(wc);wc.lpfnWndProc=proc;wc.lpszClassName=L"SwcMapped";
 if(!RegisterClassExW(&wc))return fail();
 HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"SWC",WS_OVERLAPPEDWINDOW,0,0,1920,1080,0,0,0,0);
 if(!hwnd)return fail();
 OutputDebugStringA("SWC RegisterClassExW + CreateWindowExW OK");
 ID3D11Device* device=0;ID3D11DeviceContext* context=0;D3D_FEATURE_LEVEL got;
 D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
 if(D3D11CreateDevice(0,D3D_DRIVER_TYPE_HARDWARE,0,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,&device,&got,&context)!=S_OK)return fail();
 IDXGIFactory2* factory=0;
 if(CreateDXGIFactory1(__uuidof(IDXGIFactory2),(void**)&factory)!=S_OK)return fail();
 // Both ABI entry points must work; release first chain before legacy fallback.
 for(unsigned pass=0;pass<2;++pass){
  IDXGISwapChain* chain=0;
  DXGI_SWAP_CHAIN_DESC1 d={};d.Width=1920;d.Height=1080;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
  d.SampleDesc.Count=1;d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;d.BufferCount=2;
  d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;d.Scaling=DXGI_SCALING_STRETCH;d.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
  if(!pass){
   IDXGISwapChain1* c=0;
   if(factory->CreateSwapChainForHwnd(device,hwnd,&d,0,0,&c)!=S_OK)return fail();chain=c;
   OutputDebugStringA("SWC CreateSwapChainForHwnd hr=0x00000000");
  }else{
   DXGI_SWAP_CHAIN_DESC old={};old.BufferDesc.Width=d.Width;old.BufferDesc.Height=d.Height;old.BufferDesc.Format=d.Format;
   old.SampleDesc=d.SampleDesc;old.BufferUsage=d.BufferUsage;old.BufferCount=d.BufferCount;old.OutputWindow=hwnd;
   old.Windowed=TRUE;old.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
   IDXGIFactory* legacy=0;
   if(factory->QueryInterface(__uuidof(IDXGIFactory),(void**)&legacy)!=S_OK)return fail();
   HRESULT hr=legacy->CreateSwapChain(device,&old,&chain);legacy->Release();
   if(hr!=S_OK)return fail();
   OutputDebugStringA("SWC CreateSwapChain legacy hr=0x00000000");
  }
  ID3D11Texture2D* buffer=0;ID3D11RenderTargetView* target=0;
  if(chain->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&buffer)!=S_OK)return fail();
  HRESULT hr=device->CreateRenderTargetView(buffer,0,&target);buffer->Release();if(hr!=S_OK)return fail();
  for(unsigned frame=0;frame<300;++frame){
   MSG msg;while(PeekMessageW(&msg,0,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
   const float color[]={0.05f,0.75f,0.25f,1.0f};context->ClearRenderTargetView(target,color);
   if(chain->Present(1,0)!=S_OK)return fail();
  }
  OutputDebugStringA(pass?"SWC legacy presented=300 hr=0x00000000":"SWC hwnd presented=300 hr=0x00000000");
  if(!pass){target->Release();chain->Release();context->ClearState();context->Flush();}
  // Final chain remains visible for Device Portal capture.
 }
 return 42;
}
