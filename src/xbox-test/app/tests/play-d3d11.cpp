// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that creates a D3D11 device through Play's
// D3D11CreateDevice and makes calls the device rejects, as a game does when
// it asks for something the console's d3d11 lacks. Exit code 42 = both calls
// failed the way the device reports; the diagnostics are checked in the log.
#include <d3d11.h>
extern "C" {
int d3d_anchor;
void* d3d_relocation=&d3d_anchor;
DWORD d3d_entry() {
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;D3D_FEATURE_LEVEL got{};
    const D3D_FEATURE_LEVEL asked[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,asked,2,D3D11_SDK_VERSION,&device,&got,&context);
    if(FAILED(hr))   // an implementation without 11_1 rejects the list outright
        hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,asked+1,1,D3D11_SDK_VERSION,&device,&got,&context);
    if(FAILED(hr)||!device)return 10;
    // A texture with no format: the device must refuse it, and Play must log it.
    D3D11_TEXTURE2D_DESC texture{};texture.Width=256;texture.Height=256;texture.MipLevels=1;texture.ArraySize=1;
    texture.Format=DXGI_FORMAT_UNKNOWN;texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_DEFAULT;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* created=nullptr;
    if(!FAILED(device->CreateTexture2D(&texture,nullptr,&created)))return 11;
    // The same failure again must not be logged twice; a different one is.
    texture.Width=64;
    if(!FAILED(device->CreateTexture2D(&texture,nullptr,&created)))return 12;
    D3D11_BUFFER_DESC buffer{};buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;   // zero bytes
    ID3D11Buffer* made=nullptr;
    if(!FAILED(device->CreateBuffer(&buffer,nullptr,&made)))return 13;
    UINT support=0;
    if(!FAILED(device->CheckFormatSupport(DXGI_FORMAT(0x7fff),&support))&&support)return 14;
    return 42;
}
}
