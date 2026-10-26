// mingw x64 PE: user32 imports bind ONLY to uwph-user32.dll; graphics to OS.
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <xinput.h>
extern "C" __declspec(dllimport) IUnknown* uwph_corewindow(HWND);
static void (*logline)(const char*);
static unsigned sizes,activations,paints;
static IDXGISwapChain1* held_sc;
static ID3D11DeviceContext* held_ctx;
static ID3D11RenderTargetView* held_rt;
extern "C" __declspec(dllexport) HRESULT present_window_test(){
 if(!held_sc)return E_UNEXPECTED;
 const float color[]={0.08f,0.65f,0.22f,1};held_ctx->ClearRenderTargetView(held_rt,color);return held_sc->Present(1,0);
}

static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_SIZE){++sizes;logline("WM_SIZE");}if(m==WM_ACTIVATE){++activations;logline("WM_ACTIVATE");}if(m==WM_PAINT){++paints;logline("WM_PAINT");}if(m==WM_KEYDOWN)logline("WM_KEYDOWN");if(m==WM_KEYUP)logline("WM_KEYUP");if(m==WM_MOUSEMOVE)logline("WM_MOUSEMOVE");if(m==WM_DESTROY){logline("WM_DESTROY");PostQuitMessage(0);}return DefWindowProcW(h,m,w,l);
}
static unsigned destroyed, nonclient_destroyed;
static LRESULT CALLBACK lifecycle_proc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_DESTROY){++destroyed;DestroyWindow(h);} // reentrant destruction must be bounded
 if(m==WM_NCDESTROY)++nonclient_destroyed;
 return DefWindowProcW(h,m,w,l);
}
static DWORD WINAPI wrong_thread(void*){
 WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=lifecycle_proc;wc.lpszClassName=L"WrongThread";
 SetLastError(0);return !RegisterClassExW(&wc)&&GetLastError()==ERROR_INVALID_THREAD_ID?0:1;
}
static bool check_lifecycle(){
 HANDLE thread=CreateThread(nullptr,0,wrong_thread,nullptr,0,nullptr);
 if(!thread)return false;
 DWORD result=1;WaitForSingleObject(thread,INFINITE);GetExitCodeThread(thread,&result);CloseHandle(thread);
 if(result)return false;
 WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=lifecycle_proc;wc.lpszClassName=L"Lifecycle";
 ATOM atom=RegisterClassExW(&wc);if(!atom)return false;
 if(CreateWindowExW(0,(LPCWSTR)(ULONG_PTR)atom,L"child",WS_CHILD,0,0,1,1,nullptr,nullptr,nullptr,nullptr))return false;
 HWND h=CreateWindowExW(0,(LPCWSTR)(ULONG_PTR)atom,L"lifecycle",0,0,0,1,1,nullptr,nullptr,nullptr,nullptr);
 if(!h||!PostMessageW(h,WM_CLOSE,0,0))return false;
 MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE))DispatchMessageW(&msg);
 RECT r{};return destroyed==1&&nonclient_destroyed==1&&!GetClientRect(h,&r)&&!DestroyWindow(h);
}
extern "C" __declspec(dllexport) int run_window_test(void(*log)(const char*)){
 logline=log;log("mingw x64 window PE entered");
 if(!check_lifecycle())return 28;log("PASS UI thread guard, child rejection, WM_CLOSE and reentrant destroy");
 WSADATA ws{};if(WSAStartup(MAKEWORD(2,2),&ws))return 20;WSACleanup();log("PASS real ws2_32 WSAStartup");
 BYTE random[16]{};if(BCryptGenRandom(nullptr,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return 21;log("PASS real bcrypt BCryptGenRandom");
 GUID guid{};if(FAILED(CoCreateGuid(&guid)))return 22;log("PASS real ole32 CoCreateGuid");
 DWORD unused=0;SetLastError(0);GetFileVersionInfoSizeW(L"nonexistent-w32u-file.dll",&unused);log("called real version GetFileVersionInfoSizeW (missing file expected)");WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=proc;wc.lpszClassName=L"UWPH.WindowTest";
 if(!RegisterClassExW(&wc))return 1;
 HWND h=CreateWindowExW(0,wc.lpszClassName,L"Xbox Steam window test",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,nullptr,nullptr);if(!h)return 2;ShowWindow(h,SW_SHOW);
 RECT initial{};if(!GetClientRect(h,&initial)||initial.right<=0||initial.bottom<=0)return 24;
 if(RegisterClassExW(&wc)!=0)return 25;log("PASS duplicate class rejected");
 if(DestroyWindow((HWND)(ULONG_PTR)0x1234))return 26;log("PASS invalid HWND rejected");
 ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;D3D_FEATURE_LEVEL fl{};D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_0};
 HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,requested,1,D3D11_SDK_VERSION,&dev,&fl,&ctx);if(FAILED(hr)){log("FAIL D3D11CreateDevice");return 3;}log("PASS real D3D11CreateDevice FL11_0");
 IDXGIDevice* gd=nullptr;IDXGIAdapter* ad=nullptr;IDXGIFactory2* fac=nullptr;IDXGISwapChain1* sc=nullptr;ID3D11Texture2D* bb=nullptr;ID3D11RenderTargetView* rt=nullptr;
 if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory2),(void**)&fac)))return 23;fac->Release();fac=nullptr;log("PASS real dxgi CreateDXGIFactory1");
 dev->QueryInterface(__uuidof(IDXGIDevice),(void**)&gd);if(!gd)return 4;gd->GetAdapter(&ad);if(!ad)return 5;ad->GetParent(__uuidof(IDXGIFactory2),(void**)&fac);if(!fac)return 6;
 IUnknown* core=uwph_corewindow(h);if(!core)return 7;RECT r{};GetClientRect(h,&r);
 DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=r.right;sd.Height=r.bottom;sd.Format=DXGI_FORMAT_B8G8R8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;sd.Scaling=DXGI_SCALING_STRETCH;sd.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
 hr=fac->CreateSwapChainForCoreWindow(dev,core,&sd,nullptr,&sc);core->Release();if(FAILED(hr)){log("FAIL CreateSwapChainForCoreWindow");return 8;}log("PASS real DXGI swapchain mapped from HWND");
 hr=sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb);if(FAILED(hr))return 9;hr=dev->CreateRenderTargetView(bb,nullptr,&rt);if(FAILED(hr))return 10;
 for(unsigned frame=0;frame<180;++frame){RECT bounds{};if(!GetClientRect(h,&bounds)||bounds.right!=initial.right||bounds.bottom!=initial.bottom)return 27;PostMessageW(h,WM_APP,frame,0);MSG msg{};if(GetMessageW(&msg,nullptr,0,0)<=0)return 11;TranslateMessage(&msg);DispatchMessageW(&msg);while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
 const float color[]={0.08f,0.65f,0.22f,1};ctx->ClearRenderTargetView(rt,color);hr=sc->Present(1,0);if(FAILED(hr))return 12;
 if(frame==0){XINPUT_STATE state{};DWORD status=XInputGetState(0,&state);log(status==ERROR_SUCCESS?"XInput pad 0 connected":"XInput pad 0 disconnected (no controller proof)");}}
 log("PASS presented 180 colored frames; HWND client bounds stable");DestroyWindow(h);MSG quit{};if(GetMessageW(&quit,nullptr,0,0)!=0||quit.message!=WM_QUIT)return 13;
 held_rt=rt;held_sc=sc;held_ctx=ctx; // host keeps presenting for screenshot; process owns these final references
 bb->Release();fac->Release();ad->Release();gd->Release();dev->Release();
 if(!sizes||!activations||!paints)return 14;log("PASS WM_SIZE WM_ACTIVATE WM_PAINT and WM_QUIT");return 0;
}
