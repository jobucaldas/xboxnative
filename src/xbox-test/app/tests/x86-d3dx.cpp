// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(D3DX_DECLARATIONS_ONLY)
#include <d3dx9math.h>
#include <shlwapi.h>
#include <type_traits>
using LookAt = D3DXMATRIX* (WINAPI*)(D3DXMATRIX*,const D3DXVECTOR3*,const D3DXVECTOR3*,const D3DXVECTOR3*);
using Multiply = D3DXMATRIX* (WINAPI*)(D3DXMATRIX*,const D3DXMATRIX*,const D3DXMATRIX*);
using Fov = D3DXMATRIX* (WINAPI*)(D3DXMATRIX*,FLOAT,FLOAT,FLOAT,FLOAT);
using Rotate = D3DXMATRIX* (WINAPI*)(D3DXMATRIX*,FLOAT);
using Ypr = D3DXMATRIX* (WINAPI*)(D3DXMATRIX*,FLOAT,FLOAT,FLOAT);
using Scale = D3DXMATRIX* (WINAPI*)(D3DXMATRIX*,FLOAT,FLOAT,FLOAT);
using Translate = Scale;
using Normalize = D3DXVECTOR3* (WINAPI*)(D3DXVECTOR3*,const D3DXVECTOR3*);
using Transform = D3DXVECTOR4* (WINAPI*)(D3DXVECTOR4*,const D3DXVECTOR3*,const D3DXMATRIX*);
using TransformArray = D3DXVECTOR4* (WINAPI*)(D3DXVECTOR4*,UINT,const D3DXVECTOR3*,UINT,const D3DXMATRIX*,UINT);
using Coord = D3DXVECTOR3* (WINAPI*)(D3DXVECTOR3*,const D3DXVECTOR3*,const D3DXMATRIX*);
using CoordArray = D3DXVECTOR3* (WINAPI*)(D3DXVECTOR3*,UINT,const D3DXVECTOR3*,UINT,const D3DXMATRIX*,UINT);
using FindFile = LPSTR (WINAPI*)(LPCSTR);
using RemoveExtension = void (WINAPI*)(LPSTR);
using RemoveFileSpec = BOOL (WINAPI*)(LPSTR);
static_assert(std::is_same<decltype(&PathFindFileNameA),FindFile>::value);
static_assert(std::is_same<decltype(&PathRemoveExtensionA),RemoveExtension>::value);
static_assert(std::is_same<decltype(&PathRemoveFileSpecA),RemoveFileSpec>::value);
static_assert(std::is_same<decltype(&D3DXMatrixLookAtLH),LookAt>::value);
static_assert(std::is_same<decltype(&D3DXMatrixMultiply),Multiply>::value);
static_assert(std::is_same<decltype(&D3DXMatrixPerspectiveFovLH),Fov>::value);
static_assert(std::is_same<decltype(&D3DXMatrixRotationX),Rotate>::value && std::is_same<decltype(&D3DXMatrixRotationY),Rotate>::value && std::is_same<decltype(&D3DXMatrixRotationZ),Rotate>::value);
static_assert(std::is_same<decltype(&D3DXMatrixRotationYawPitchRoll),Ypr>::value);
static_assert(std::is_same<decltype(&D3DXMatrixScaling),Scale>::value && std::is_same<decltype(&D3DXMatrixTranslation),Translate>::value);
static_assert(std::is_same<decltype(&D3DXVec3Normalize),Normalize>::value);
static_assert(std::is_same<decltype(&D3DXVec3Transform),Transform>::value && std::is_same<decltype(&D3DXVec3TransformArray),TransformArray>::value);
static_assert(std::is_same<decltype(&D3DXVec3TransformCoord),Coord>::value && std::is_same<decltype(&D3DXVec3TransformCoordArray),CoordArray>::value);
#else
#include <cstdio>
#include <cstring>
#include "../host/x86/x86_guest.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while (0)
static uint32_t bits(float f){uint32_t v;std::memcpy(&v,&f,4);return v;}
static float value(Memory& m,uint32_t p){uint32_t v=m.r32(p);float f;std::memcpy(&f,&v,4);return f;}
static bool near(float a,float b){return std::fabs(a-b)<0.0002f;}
int main(){
 Memory mem;mem.map(0x1000,0x20000,PROT_R|PROT_W);ThunkTable th;Host host;Registry reg;Guest g(mem,host,reg);g.register_all();Cpu cpu(mem,th);cpu.reg[ESP]=0x1f000;
 const char* names[]={"d3dxmatrixlookatlh","d3dxmatrixmultiply","d3dxmatrixperspectivefovlh","d3dxmatrixrotationx","d3dxmatrixrotationy","d3dxmatrixrotationyawpitchroll","d3dxmatrixrotationz","d3dxmatrixscaling","d3dxmatrixtranslation","d3dxvec3normalize","d3dxvec3transform","d3dxvec3transformarray","d3dxvec3transformcoord","d3dxvec3transformcoordarray"};
 const unsigned arity[]={4,3,5,2,2,4,2,4,4,2,3,6,3,6};
 for(unsigned i=0;i<14;++i){auto it=reg.map.find({"d3dx9_36.dll",names[i]});CHECK(it!=reg.map.end());if(it!=reg.map.end()){CHECK(it->second.conv==Conv::Stdcall);CHECK(it->second.argc==arity[i]);}}
 auto call=[&](const char* n,std::initializer_list<uint32_t> a){auto it=reg.map.find({"d3dx9_36.dll",n});for(unsigned i=0;i<a.size();++i)mem.w32(cpu.reg[ESP]+4+4*i,*(a.begin()+i));return it->second.fn(cpu);};
 auto put=[&](uint32_t p,std::initializer_list<float> a){for(float v:a){mem.w32(p,bits(v));p+=4;}};
 put(0x2000,{2,3,4});CHECK(call("d3dxmatrixscaling",{0x3000,bits(2),bits(3),bits(4)})==0x3000);CHECK(value(mem,0x3000)==2&&value(mem,0x3014)==3&&value(mem,0x3028)==4);
 CHECK(call("d3dxmatrixtranslation",{0x3000,bits(5),bits(6),bits(7)})==0x3000);CHECK(value(mem,0x3030)==5&&value(mem,0x3034)==6&&value(mem,0x3038)==7);
 put(0x4000,{3,0,4});CHECK(call("d3dxvec3normalize",{0x5000,0x4000})==0x5000);CHECK(near(value(mem,0x5000),.6f)&&near(value(mem,0x5008),.8f));
 put(0x6000,{1,0,0,0,0,1,0,0,0,0,1,0,1,2,3,2});put(0x6100,{2,4,6});CHECK(call("d3dxvec3transform",{0x6200,0x6100,0x6000})==0x6200);CHECK(value(mem,0x620c)==2&&value(mem,0x6200)==3&&value(mem,0x6208)==9);
 CHECK(call("d3dxvec3transformcoord",{0x6300,0x6100,0x6000})==0x6300);CHECK(near(value(mem,0x6300),1.5f)&&near(value(mem,0x6304),3)&&near(value(mem,0x6308),4.5f));
 put(0x7000,{0,0,0,1,2,3});CHECK(call("d3dxvec3transformcoordarray",{0x7100,16,0x7000,12,0x6000,2})==0x7100);CHECK(value(mem,0x7104)==1&&near(value(mem,0x7110),1)&&near(value(mem,0x7118),3));
 CHECK(call("d3dxvec3transformarray",{0x7800,20,0x7000,12,0x6000,2})==0x7800);CHECK(value(mem,0x780c)==2&&value(mem,0x7814)==2&&value(mem,0x7820)==2);
 CHECK(call("d3dxmatrixperspectivefovlh",{0x7200,bits(1.57079632679f),bits(2),bits(1),bits(10)})==0x7200);CHECK(near(value(mem,0x7200),.5f)&&near(value(mem,0x7214),1)&&near(value(mem,0x7228),10.0f/9)&&near(value(mem,0x7238),-10.0f/9));
 put(0x7300,{0,0,0});put(0x7310,{0,0,1});put(0x7320,{0,1,0});CHECK(call("d3dxmatrixlookatlh",{0x7400,0x7300,0x7310,0x7320})==0x7400);CHECK(near(value(mem,0x7400),1)&&near(value(mem,0x7414),1)&&near(value(mem,0x7428),1));
 CHECK(call("d3dxmatrixrotationz",{0x7500,bits(1.57079632679f)})==0x7500);CHECK(near(value(mem,0x7504),1)&&near(value(mem,0x7510),-1));
 CHECK(call("d3dxmatrixrotationx",{0x7500,bits(1.57079632679f)})==0x7500);CHECK(near(value(mem,0x7518),1)&&near(value(mem,0x7524),-1));
 CHECK(call("d3dxmatrixrotationy",{0x7500,bits(1.57079632679f)})==0x7500);CHECK(near(value(mem,0x7508),-1)&&near(value(mem,0x7520),1));
 CHECK(call("d3dxmatrixrotationyawpitchroll",{0x7600,0,bits(0),bits(1.57079632679f)})==0x7600);CHECK(near(value(mem,0x7604),1)&&near(value(mem,0x7610),-1));
 CHECK(call("d3dxmatrixmultiply",{0x7700,0x3000,0x6000})==0x7700);CHECK(near(value(mem,0x7700),1)&&near(value(mem,0x7730),6)&&near(value(mem,0x7734),8)&&near(value(mem,0x7738),10));
 const char* pathApis[]={"pathfindfilenamea","pathremoveextensiona","pathremovefilespeca"};const unsigned pathArity[]={1,1,1};for(unsigned i=0;i<3;++i){auto it=reg.map.find({"shlwapi.dll",pathApis[i]});CHECK(it!=reg.map.end());if(it!=reg.map.end()){CHECK(it->second.conv==Conv::Stdcall);CHECK(it->second.argc==pathArity[i]);}}
 auto pathCall=[&](const char* n,uint32_t p){mem.w32(cpu.reg[ESP]+4,p);return reg.map.at({"shlwapi.dll",n}).fn(cpu);};
 uint32_t path=g.put_str("C:\\games\\melty.exe");CHECK(pathCall(pathApis[0],path)==path+9);pathCall(pathApis[1],path);CHECK(mem.read_cstr(path)=="C:\\games\\melty");uint32_t spaced=g.put_str("C:\\game dir\\mel.ty.exe");pathCall(pathApis[1],spaced);CHECK(mem.read_cstr(spaced)=="C:\\game dir\\mel.ty");CHECK(pathCall(pathApis[2],path)==1);CHECK(mem.read_cstr(path)=="C:\\games");
 uint32_t root=g.put_str("C:\\");CHECK(pathCall(pathApis[2],root)==0);uint32_t trailing=g.put_str("C:\\games\\");CHECK(pathCall(pathApis[2],trailing)==1);CHECK(mem.read_cstr(trailing)=="C:\\games");
 std::printf("x86-d3dx: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#endif
