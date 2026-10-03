#include "integration.h"
#include "MinHook.h"
#include <cstdlib>
#include <cmath>

namespace left4craft {
namespace {
struct Vertex { float x,y,z; DWORD color; float u,v; };
struct Section { int x,y,z; Vertex* vertices; unsigned count; std::uint64_t touched; };
Section sections[256]{};
IDirect3DTexture9* atlas{};
IDirect3DTexture9* hud{};
unsigned atlasW{},atlasH{},hudW{},hudH{};
std::uint64_t hudFrame{},clock_{};
using EndScene=HRESULT(WINAPI*)(IDirect3DDevice9*);
using Reset=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
EndScene endOriginal{};Reset resetOriginal{};
void* endAddress{};void* resetAddress{};
template<class T> void release(T*& object) {if(object){object->Release();object=nullptr;}}
void clearSections(){for(auto& s:sections){std::free(s.vertices);s={};}}
bool upload(IDirect3DDevice9* d,IDirect3DTexture9*& texture,unsigned& oldW,unsigned& oldH,
            unsigned w,unsigned h,const std::uint8_t* rgba,bool dynamic) {
    if(!w||!h||w>8192||h>8192) return false;
    if(!texture||w!=oldW||h!=oldH){
        release(texture);
        if(FAILED(d->CreateTexture(w,h,1,dynamic?D3DUSAGE_DYNAMIC:0,D3DFMT_A8R8G8B8,
                    dynamic?D3DPOOL_DEFAULT:D3DPOOL_MANAGED,&texture,nullptr)))return false;
        oldW=w;oldH=h;
    }
    D3DLOCKED_RECT lock{};if(FAILED(texture->LockRect(0,&lock,nullptr,dynamic?D3DLOCK_DISCARD:0)))return false;
    for(unsigned y=0;y<h;++y){auto* dst=static_cast<DWORD*>(lock.pBits)+y*(lock.Pitch/4);auto* src=reinterpret_cast<const DWORD*>(rgba)+y*w;
        for(unsigned x=0;x<w;++x){auto c=src[x];dst[x]=(c&0xff00ff00)|((c&255)<<16)|((c>>16)&255);}}
    texture->UnlockRect(0);return true;
}
void consume(IDirect3DDevice9* d,unsigned type,const std::uint8_t* payload,unsigned bytes){
    if(type==p::kRenClearAll){clearSections();return;}
    if(type==p::kRenAtlas && bytes>=sizeof(p::RenAtlas)){
        auto a=*reinterpret_cast<const p::RenAtlas*>(payload);
        if(a.width<=8192 && a.height<=8192 && std::uint64_t(a.width)*a.height*4<=bytes-sizeof(a))
            upload(d,atlas,atlasW,atlasH,a.width,a.height,payload+sizeof(a),false);
        return;
    }
    if(type==p::kRenAtlasRegion && atlas && bytes>=sizeof(p::RenAtlasRegion)){
        auto a=*reinterpret_cast<const p::RenAtlasRegion*>(payload);
        if(a.x+a.width>atlasW||a.y+a.height>atlasH||std::uint64_t(a.width)*a.height*4>bytes-sizeof(a))return;
        RECT rect{LONG(a.x),LONG(a.y),LONG(a.x+a.width),LONG(a.y+a.height)};D3DLOCKED_RECT lock{};
        if(SUCCEEDED(atlas->LockRect(0,&lock,&rect,0))){
            auto* src=reinterpret_cast<const DWORD*>(payload+sizeof(a));
            for(unsigned y=0;y<a.height;++y)for(unsigned x=0;x<a.width;++x){auto c=src[y*a.width+x];
                reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=(c&0xff00ff00)|((c&255)<<16)|((c>>16)&255);}
            atlas->UnlockRect(0);
        }return;
    }
    if(type!=p::kRenSection||bytes<sizeof(p::RenSection))return;
    auto r=*reinterpret_cast<const p::RenSection*>(payload);
    if(r.vertexCount>120000||r.vertexCount%3||std::uint64_t(r.vertexCount)*sizeof(p::RenVertex)>bytes-sizeof(r))return;
    Section* dst=nullptr;Section* oldest=&sections[0];
    for(auto& s:sections){if(s.x==r.sx&&s.y==r.sy&&s.z==r.sz&&s.vertices){dst=&s;break;}
        if(!s.vertices&&!dst)dst=&s;if(s.touched<oldest->touched)oldest=&s;}
    if(!dst)dst=oldest;
    std::free(dst->vertices);*dst={};if(!r.vertexCount)return;
    auto* vertices=static_cast<Vertex*>(std::malloc(r.vertexCount*sizeof(Vertex)));if(!vertices)return;
    auto* src=reinterpret_cast<const p::RenVertex*>(payload+sizeof(r));
    for(unsigned i=0;i<r.vertexCount;++i){
        auto pos=mcToSource({double(r.sx)*16+src[i].x,double(r.sy)*16+src[i].y,double(r.sz)*16+src[i].z});
        if(!std::isfinite(pos.x)||!std::isfinite(pos.y)||!std::isfinite(pos.z)){std::free(vertices);return;}
        auto c=src[i].color;
        vertices[i]={float(pos.x),float(pos.y),float(pos.z),(c&0xff00ff00)|((c&255)<<16)|((c>>16)&255),src[i].u,src[i].v};
    }
    *dst={r.sx,r.sy,r.sz,vertices,r.vertexCount,++clock_};
}
HRESULT WINAPI endHook(IDirect3DDevice9* device){renderFrame(device);return endOriginal(device);}
HRESULT WINAPI resetHook(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS* pp){release(hud);hudW=hudH=0;hudFrame=0;return resetOriginal(device,pp);}
}

bool renderInstall(){
    auto* d3d=Direct3DCreate9(D3D_SDK_VERSION);if(!d3d)return false;
    HWND window=CreateWindowExA(0,"STATIC","Left4Craft render probe",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_UNKNOWN;
    IDirect3DDevice9* device{};
    auto hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&device);
    bool ok=false;
    if(SUCCEEDED(hr)){
        auto** table=*reinterpret_cast<void***>(device);endAddress=table[42];resetAddress=table[16];
        ok=MH_CreateHook(endAddress,reinterpret_cast<void*>(endHook),reinterpret_cast<void**>(&endOriginal))==MH_OK
          &&MH_CreateHook(resetAddress,reinterpret_cast<void*>(resetHook),reinterpret_cast<void**>(&resetOriginal))==MH_OK;
        device->Release();
    }
    d3d->Release();if(window)DestroyWindow(window);
    return ok;
}
void renderShutdown(){release(hud);release(atlas);clearSections();hudFrame=0;}
void renderFrame(IDirect3DDevice9* d){
    if(!hostLink||!hostLink->valid()||!hostLink->minecraftAlive())return;
    hostLink->render([&](unsigned type,const std::uint8_t* payload,unsigned bytes){consume(d,type,payload,bytes);});
    if(!InterlockedCompareExchange(&controlling,0,0)||clientMenu())return;
    IDirect3DStateBlock9* saved{};if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&saved)))return;saved->Capture();
    D3DMATRIX oldWorld{},oldView{},oldProjection{};d->GetTransform(D3DTS_WORLD,&oldWorld);d->GetTransform(D3DTS_VIEW,&oldView);d->GetTransform(D3DTS_PROJECTION,&oldProjection);
    d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_FOGENABLE,FALSE);
    d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
    d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE);d->SetRenderState(D3DRS_ALPHAREF,32);d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER);
    d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
    d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);d->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    D3DMATRIX identity{};identity._11=identity._22=identity._33=identity._44=1;
    d->SetTransform(D3DTS_WORLD,&identity);d->SetTransform(D3DTS_VIEW,&identity);
    // This build's IVEngineClient world-to-screen matrix is slot 37 (confirmed in binary).
    auto* matrix=method<const float*(__thiscall*)(void*)>(engineClient,37)(engineClient);D3DMATRIX projection{};
    for(int i=0;i<4;++i)for(int j=0;j<4;++j)projection.m[i][j]=matrix[j*4+i];
    d->SetTransform(D3DTS_PROJECTION,&projection);d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1);
    d->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
    d->SetTexture(0,atlas);
    if(atlas)for(auto& s:sections)if(s.vertices&&s.count)d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,s.count/3,s.vertices,sizeof(Vertex));
    p::OverlaySlotHdr header{};auto* pixels=hostLink->overlay(header);
    if(pixels&&header.frameId!=hudFrame&&upload(d,hud,hudW,hudH,header.width,header.height,pixels,true))hudFrame=header.frameId;
    if(hud){
        D3DVIEWPORT9 viewport{};d->GetViewport(&viewport);
        struct ScreenVertex{float x,y,z,rhw;DWORD color;float u,v;};
        float top=(header.flags&1)?1.f:0.f,bottom=1.f-top;
        float x=float(viewport.X)-0.5f,y=float(viewport.Y)-0.5f,w=float(viewport.Width),h=float(viewport.Height);
        ScreenVertex quad[4]={{x,y,0,1,0xffffffff,0,top},{x+w,y,0,1,0xffffffff,1,top},{x,y+h,0,1,0xffffffff,0,bottom},{x+w,y+h,0,1,0xffffffff,1,bottom}};
        d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);
        d->SetTexture(0,hud);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(ScreenVertex));
    }
    p::McState mc{};
    if(readPlayer(mc)&&(mc.flags&p::kMcScreenOpen)){
      float x,y;cursorPosition(x,y);struct Cursor{float x,y,z,rhw;DWORD color;float u,v;};
      Cursor arrow[3]={{x,y,0,1,0xffffffff,0,0},{x+3,y+16,0,1,0xffffffff,0,0},{x+12,y+11,0,1,0xffffffff,0,0}};
      d->SetTexture(0,nullptr);d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2);
      d->SetRenderState(D3DRS_ZENABLE,FALSE);d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,arrow,sizeof(Cursor));
    }
    saved->Apply();saved->Release();d->SetTransform(D3DTS_WORLD,&oldWorld);d->SetTransform(D3DTS_VIEW,&oldView);d->SetTransform(D3DTS_PROJECTION,&oldProjection);
}
}
