#include "integration.h"
#include "MinHook.h"
#include <cstdlib>
#include <cmath>

namespace left4craft {
namespace {
struct Vertex { float x,y,z; DWORD color; float u,v; DWORD baseColor; unsigned flags; };
struct Section { int x,y,z; Vertex* vertices; unsigned count; std::uint64_t touched,litAt; };
Section sections[256]{};
IDirect3DTexture9* atlas{};
IDirect3DTexture9* hud{};
unsigned atlasW{},atlasH{},hudW{},hudH{};
std::uint64_t hudFrame{},clock_{};
using EndScene=HRESULT(WINAPI*)(IDirect3DDevice9*);
using Reset=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
EndScene endOriginal{};Reset resetOriginal{};
void* endAddress{};void* resetAddress{};
// Keep world drawing at the proven EndScene path. Source SceneEnd may still
// target an offscreen view, and its viewport is not necessarily the window size.
struct DepthVertex {float x,y,z;};
struct DepthRegion {int x,y,z;DepthVertex* vertices;unsigned count;std::uint64_t touched;};
DepthRegion depthRegions[64]{};SRWLOCK depthLock=SRWLOCK_INIT;
std::uint64_t depthClock{};unsigned depthBytes{};
IDirect3DSurface9* worldDepth{};D3DSURFACE_DESC worldDepthDesc{};
void drawPass(IDirect3DDevice9* d,bool world);
struct LightCell {int x,y,z;DWORD rgb;std::uint64_t stamp;};
LightCell lightCells[4096]{};unsigned lightBudget{};
DWORD sourceLight(float x,float y,float z){
    int ix=int(std::floor(x/16)),iy=int(std::floor(y/16)),iz=int(std::floor(z/16));
    unsigned hash=(unsigned(ix)*73856093u^unsigned(iy)*19349663u^unsigned(iz)*83492791u)&4095;
    auto& cell=lightCells[hash];auto now=GetTickCount64();
    bool matches=cell.stamp&&cell.x==ix&&cell.y==iy&&cell.z==iz;
    if((!matches||now-cell.stamp>250)&&lightBudget){
        --lightBudget;float point[]={x,y,z},light[3]{};
        // GetLightForPoint: verified slot 1, MSVC hidden return pointer, ret 12.
        method<void(__thiscall*)(void*,float*,const float*,bool)>(engineClient,1)(engineClient,light,point,true);
        unsigned channel[3]{};
        for(int i=0;i<3;++i){float v=std::isfinite(light[i])?light[i]:0;if(v<0)v=0;if(v>1)v=1;channel[i]=unsigned(std::pow(v,1.f/2.2f)*255);}
        cell={ix,iy,iz,(channel[0]<<16)|(channel[1]<<8)|channel[2],now};matches=true;
    }
    return matches?cell.rgb:0x606060;
}
DWORD modulate(DWORD base,DWORD light){
    return (base&0xff000000)|((((base>>16)&255)*((light>>16)&255)/255)<<16)
        |((((base>>8)&255)*((light>>8)&255)/255)<<8)|((base&255)*(light&255)/255);
}
void lightSection(Section& section){
    // Sample just outside each face, so a vertex on a native wall does not sample solid space.
    static const float normals[7][3]={{0,0,0},{0,0,-1},{0,0,1},{0,1,0},{0,-1,0},{-1,0,0},{1,0,0}};
    for(unsigned i=0;i<section.count;i+=3){auto* v=section.vertices+i;unsigned n=(v[0].flags>>4)&7;if(n>6)n=0;
        auto light=sourceLight((v[0].x+v[1].x+v[2].x)/3+normals[n][0]*2,
            (v[0].y+v[1].y+v[2].y)/3+normals[n][1]*2,(v[0].z+v[1].z+v[2].z)/3+normals[n][2]*2);
        for(int j=0;j<3;++j)v[j].color=modulate(v[j].baseColor,light);
    }
}
void entityQuad(IDirect3DDevice9* d,const Point points[4],const float uv[4],DWORD color){
    Vertex v[6]{};const int index[]={0,1,2,0,2,3};
    for(int i=0;i<6;++i){int k=index[i];auto p=mcToSource(points[k]);v[i]={float(p.x),float(p.y),float(p.z),color,
        (k==0||k==3)?uv[0]:uv[2],(k<2)?uv[1]:uv[3],color,0};}
    d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,v,sizeof(Vertex));
}
void drawArrows(IDirect3DDevice9* d){
    static p::WorldEntities entities{};if(!hostLink->worldEntities(entities))return;
    for(unsigned i=0;i<entities.count;++i){const auto& e=entities.entities[i];if(e.kind!=p::kWeArrow)continue;
        if(!std::isfinite(e.x)||!std::isfinite(e.y)||!std::isfinite(e.z)||!std::isfinite(e.yaw)||!std::isfinite(e.pitch))continue;
        float yaw=e.yaw*0.01745329252f,pitch=e.pitch*0.01745329252f;
        Point forward{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
        Point side{std::cos(yaw),0,-std::sin(yaw)};
        Point up{side.y*forward.z-side.z*forward.y,side.z*forward.x-side.x*forward.z,side.x*forward.y-side.y*forward.x};
        Point fins[2]={{(up.x+side.x)*.70710678,(up.y+side.y)*.70710678,(up.z+side.z)*.70710678},
                       {(up.x-side.x)*.70710678,(up.y-side.y)*.70710678,(up.z-side.z)*.70710678}};
        auto at=[&](double along,Point q,double across,Point r=Point{},double across2=0){return Point{e.x+forward.x*along+q.x*across+r.x*across2,e.y+forward.y*along+q.y*across+r.y*across2,e.z+forward.z*along+q.z*across+r.z*across2};};
        auto source=mcToSource({e.x,e.y,e.z});DWORD color=0xff000000|sourceLight(float(source.x),float(source.y),float(source.z));
        constexpr double k=.9/16;
        for(auto fin:fins){Point quad[]={at(-12*k,fin,-2*k),at(4*k,fin,-2*k),at(4*k,fin,2*k),at(-12*k,fin,2*k)};entityQuad(d,quad,e.uv[0],color);}
        Point back[]={at(-11*k,fins[0],-2*k,fins[1],-2*k),at(-11*k,fins[0],2*k,fins[1],-2*k),at(-11*k,fins[0],2*k,fins[1],2*k),at(-11*k,fins[0],-2*k,fins[1],2*k)};
        entityQuad(d,back,e.uv[1],color);
    }
}
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
    if(type==p::kRenClearAll){clearSections();std::memset(lightCells,0,sizeof(lightCells));return;}
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
        vertices[i]={float(pos.x),float(pos.y),float(pos.z),(c&0xff00ff00)|((c&255)<<16)|((c>>16)&255),src[i].u,src[i].v,(c&0xff00ff00)|((c&255)<<16)|((c>>16)&255),src[i].flags};
    }
    *dst={r.sx,r.sy,r.sz,vertices,r.vertexCount,++clock_};
}
HRESULT WINAPI endHook(IDirect3DDevice9* device){renderFrame(device);return endOriginal(device);}
HRESULT WINAPI resetHook(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS* pp){release(worldDepth);worldDepthDesc={};release(hud);hudW=hudH=0;hudFrame=0;return resetOriginal(device,pp);}
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
void renderClearCollision(){
    AcquireSRWLockExclusive(&depthLock);
    for(auto& r:depthRegions){std::free(r.vertices);r={};}depthBytes=0;
    ReleaseSRWLockExclusive(&depthLock);
}
void renderCollisionRegion(const p::ColRegion& region,const p::ColTri* triangles){
    if(region.count>60000)return;
    unsigned count=0;
    for(unsigned i=0;i<region.count;++i)if(!(triangles[i].flags&collisionNoVisual))count+=3;
    auto* vertices=count?static_cast<DepthVertex*>(std::malloc(count*sizeof(DepthVertex))):nullptr;
    if(count&&!vertices)return;
    unsigned at=0;
    for(unsigned i=0;i<region.count;++i){if(triangles[i].flags&collisionNoVisual)continue;
        for(int k=0;k<3;++k){auto pos=mcToSource({triangles[i].v[k*3],triangles[i].v[k*3+1],triangles[i].v[k*3+2]});
            if(!std::isfinite(pos.x)||!std::isfinite(pos.y)||!std::isfinite(pos.z)){std::free(vertices);return;}
            vertices[at++]={float(pos.x),float(pos.y),float(pos.z)};
        }
    }
    AcquireSRWLockExclusive(&depthLock);
    DepthRegion* slot=nullptr;DepthRegion* oldest=&depthRegions[0];
    for(auto& r:depthRegions){if(r.vertices&&r.x==region.minX&&r.y==region.minY&&r.z==region.minZ){slot=&r;break;}
        if(!r.vertices&&!slot)slot=&r;if(r.touched<oldest->touched)oldest=&r;}
    if(!slot)slot=oldest;
    depthBytes-=slot->count*sizeof(DepthVertex);std::free(slot->vertices);*slot={};
    // Bound the extra memory used for native-world occlusion to 16 MiB.
    while(depthBytes+count*sizeof(DepthVertex)>(16u<<20)){
        DepthRegion* victim=nullptr;for(auto& r:depthRegions)if(r.vertices&&(!victim||r.touched<victim->touched))victim=&r;
        if(!victim)break;depthBytes-=victim->count*sizeof(DepthVertex);std::free(victim->vertices);*victim={};
    }
    *slot={region.minX,region.minY,region.minZ,vertices,count,++depthClock};depthBytes+=count*sizeof(DepthVertex);
    ReleaseSRWLockExclusive(&depthLock);
}
void renderShutdown(){release(worldDepth);worldDepthDesc={};renderClearCollision();release(hud);release(atlas);clearSections();hudFrame=0;}
namespace {
bool prepareWorldDepth(IDirect3DDevice9* d){
    IDirect3DSurface9* target{};D3DSURFACE_DESC desc{};
    if(FAILED(d->GetRenderTarget(0,&target)))return false;
    HRESULT hr=target->GetDesc(&desc);target->Release();if(FAILED(hr))return false;
    if(!worldDepth||desc.Width!=worldDepthDesc.Width||desc.Height!=worldDepthDesc.Height||
        desc.MultiSampleType!=worldDepthDesc.MultiSampleType||desc.MultiSampleQuality!=worldDepthDesc.MultiSampleQuality){
        release(worldDepth);
        hr=d->CreateDepthStencilSurface(desc.Width,desc.Height,D3DFMT_D24S8,desc.MultiSampleType,desc.MultiSampleQuality,TRUE,&worldDepth,nullptr);
        if(FAILED(hr))hr=d->CreateDepthStencilSurface(desc.Width,desc.Height,D3DFMT_D16,desc.MultiSampleType,desc.MultiSampleQuality,TRUE,&worldDepth,nullptr);
        if(FAILED(hr))return false;worldDepthDesc=desc;
    }
    if(FAILED(d->SetDepthStencilSurface(worldDepth)))return false;
    if(FAILED(d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1.f,0)))return false;
    d->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE,0);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetFVF(D3DFVF_XYZ);d->SetTexture(0,nullptr);
    d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2);
    AcquireSRWLockShared(&depthLock);
    for(const auto& r:depthRegions)if(r.vertices&&r.count)d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,r.count/3,r.vertices,sizeof(DepthVertex));
    ReleaseSRWLockShared(&depthLock);
    d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1);
    d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);
    d->SetTexture(0,atlas);return true;
}
void drawPass(IDirect3DDevice9* d,bool world){
    if(!hostLink||!hostLink->valid()||!hostLink->minecraftAlive())return;
    hostLink->render([&](unsigned type,const std::uint8_t* payload,unsigned bytes){consume(d,type,payload,bytes);});
    if(!InterlockedCompareExchange(&controlling,0,0)||clientMenu())return;
    IDirect3DStateBlock9* saved{};if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&saved)))return;if(FAILED(saved->Capture())){saved->Release();return;}
    IDirect3DSurface9* previousDepth{};if(world)d->GetDepthStencilSurface(&previousDepth);
    D3DMATRIX oldWorld{},oldView{},oldProjection{};d->GetTransform(D3DTS_WORLD,&oldWorld);d->GetTransform(D3DTS_VIEW,&oldView);d->GetTransform(D3DTS_PROJECTION,&oldProjection);
    d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_FOGENABLE,FALSE);
    d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);d->SetRenderState(D3DRS_DEPTHBIAS,0);d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);
    d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
    d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
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
    d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
    d->SetTexture(0,atlas);
    if(world&&atlas){
        bool haveDepth=prepareWorldDepth(d);
        if(!haveDepth){
            // Visibility takes precedence over silently rejecting the entire world.
            d->SetDepthStencilSurface(previousDepth);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
            static bool warned=false;if(!warned){note("Private world depth unavailable: rendering visible blocks/arrows without native occlusion.");warned=true;}
        }
        lightBudget=128;auto now=GetTickCount64();
        for(auto& s:sections)if(s.vertices&&s.count){
            if(!s.litAt||now-s.litAt>250){lightSection(s);s.litAt=now;}
            d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,s.count/3,s.vertices,sizeof(Vertex));
        }
        drawArrows(d);
        static bool reported=false;if(!reported){note("World renderer v2: EndScene blocks/arrows with private BSP depth; no viewport gate.");reported=true;}
    }
    if(!world){
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
    }
    if(world){d->SetDepthStencilSurface(previousDepth);release(previousDepth);}
    saved->Apply();saved->Release();d->SetTransform(D3DTS_WORLD,&oldWorld);d->SetTransform(D3DTS_VIEW,&oldView);d->SetTransform(D3DTS_PROJECTION,&oldProjection);
}
}
void renderFrame(IDirect3DDevice9* d){drawPass(d,true);drawPass(d,false);}
}
