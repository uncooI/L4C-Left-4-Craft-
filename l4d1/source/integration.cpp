#include "integration.h"
#include "motion.h"
#include "MinHook.h"
#include "basehandle.h"
class CBasePlayer;
class CMoveData;
#include "game/shared/usercmd.h"
#include <cstdio>
#include <cmath>
#include <wincrypt.h>
namespace left4craft {
Link* hostLink{};void* engineClient{};void* serverPlayer{};
volatile LONG requested{},controlling{};
namespace {
void* entities{};void* engineVgui{};bool held[256]{};
using Move=void(__thiscall*)(void*,void*,CMoveData*);
Move clientMove{},serverMove{};
using CreateMove=bool(__thiscall*)(void*,float,CUserCmd*);CreateMove createOriginal{};
using View=void(__thiscall*)(void*,void*);View viewOriginal{};
using DrawViewmodel=bool(__thiscall*)(void*);DrawViewmodel drawViewmodelOriginal{};
bool __fastcall drawViewmodelHook(void* self,void*){p::McState state{};return readPlayer(state)?false:drawViewmodelOriginal(self);}
using Mouse=void(__thiscall*)(void*,float*,float*);Mouse mouseOriginal{};
using Damage=int(__thiscall*)(void*,const void*);Damage damageOriginal{};
int __fastcall damageHook(void* self,void*,const void* info){
 if(self==serverPlayer&&controlling&&hostLink&&hostLink->minecraftAlive()){
  float damage=*reinterpret_cast<const float*>(static_cast<const char*>(info)+0x2c);
  unsigned attacker=*reinterpret_cast<const unsigned*>(static_cast<const char*>(info)+0x28);
  if(std::isfinite(damage)&&damage>0&&damage<10000&&hostLink->input(p::kInHurt,0,int(damage*100),int(attacker)))return 0;
 }return damageOriginal(self,info);
}
float cursorX=640,cursorY=360;HWND gameWindow{};WNDPROC windowOriginal{};
bool focus(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
LRESULT CALLBACK windowHook(HWND w,UINT message,WPARAM a,LPARAM b){
 if(controlling&&hostLink&&focus()){
  if(message==WM_MOUSEWHEEL){hostLink->input(p::kInScroll,0,GET_WHEEL_DELTA_WPARAM(a));return 0;}
  if(message==WM_CHAR&&a>=32&&a<0xd800)hostLink->input(p::kInText,0,int(a));
 }
 return CallWindowProcW(windowOriginal,w,message,a,b);
}
bool down(int key){return (GetAsyncKeyState(key)&0x8000)!=0;}
void key(int vk,unsigned scan){bool value=down(vk);if(value!=held[vk]){held[vk]=value;hostLink->input(p::kInKey,scan,value);}}
void __fastcall clientMoveHook(void* self,void*,void* player,CMoveData* move){
 p::McState state{};
 int local=method<int(__thiscall*)(void*)>(engineClient,12)(engineClient);
 void* ent=method<void*(__thiscall*)(void*,int)>(entities,3)(entities,local);
 if(player==ent&&readPlayer(state)){
  auto pos=mcToSource({state.x,state.y,state.z});// This game build adds a constraint field absent from the SDK: origin is 0x9c.
  auto* bytes=reinterpret_cast<char*>(move);float origin[]={float(pos.x),float(pos.y),float(pos.z)};
  std::memcpy(bytes+0x9c,origin,12);std::memset(bytes+0x40,0,12);std::memset(bytes+0x64,0,28);return;
 }clientMove(self,player,move);
}
void __fastcall serverMoveHook(void* self,void*,void* player,CMoveData* move){
 p::McState state{};
 if(player==serverPlayer&&readPlayer(state)){
  auto pos=mcToSource({state.curX,state.curY,state.curZ});// This game build adds a constraint field absent from the SDK: origin is 0x9c.
  auto* bytes=reinterpret_cast<char*>(move);float origin[]={float(pos.x),float(pos.y),float(pos.z)};
  std::memcpy(bytes+0x9c,origin,12);std::memset(bytes+0x40,0,12);std::memset(bytes+0x64,0,28);return;
 }serverMove(self,player,move);
}
bool __fastcall createHook(void* self,void*,float time,CUserCmd* cmd){
 bool result=createOriginal(self,time,cmd);p::McState state{};
 if(cmd&&readPlayer(state)){cmd->forwardmove=cmd->sidemove=cmd->upmove=0;cmd->buttons=down('G')?(1<<5):0;cmd->impulse=0;cmd->weaponselect=0;}
 return result;
}
void __fastcall viewHook(void* self,void*,void* view){
 viewOriginal(self,view);p::McState state{};if(!readPlayer(state)||clientMenu())return;
 auto* bytes=static_cast<char*>(view);auto pos=mcToSource({state.eyeX,state.eyeY,state.eyeZ});
 auto* origin=reinterpret_cast<float*>(bytes+0x2c);origin[0]=float(pos.x);origin[1]=float(pos.y);origin[2]=float(pos.z);
 float angles[3];lookAngles(angles);std::memcpy(bytes+0x38,angles,12);
 // OverrideView runs BEFORE Source applies widescreen scaling. Store the 4:3
 // horizontal FOV here, otherwise Source applies the aspect correction twice.
 int w,h;screenSize(w,h);float fov=state.fovDeg;
 if(std::isfinite(fov)&&fov>20&&fov<150&&h>0)*reinterpret_cast<float*>(bytes+0x24)=sourceBaseFov(fov);
}
void __fastcall mouseHook(void* self,void*,float* x,float* y){
 p::McState state{};if(readPlayer(state)&&(state.flags&p::kMcScreenOpen)){
  int w,h;screenSize(w,h);cursorX+=*x;cursorY+=*y;
  if(cursorX<0)cursorX=0;if(cursorX>w-1)cursorX=float(w-1);if(cursorY<0)cursorY=0;if(cursorY>h-1)cursorY=float(h-1);
  hostLink->input(p::kInCursor,0,int(cursorX),int(cursorY));*x=*y=0;
 }mouseOriginal(self,x,y);
}
bool hash(HMODULE mod,const char* wanted){
 wchar_t path[MAX_PATH];if(!mod||!GetModuleFileNameW(mod,path,MAX_PATH))return false;
 HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
 HCRYPTPROV provider{};HCRYPTHASH value{};bool ok=CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&CryptCreateHash(provider,CALG_SHA_256,0,0,&value);
 BYTE buffer[2048];DWORD got=0;
 while(ok){if(!ReadFile(file,buffer,sizeof(buffer),&got,nullptr)){ok=false;break;}if(!got)break;ok=CryptHashData(value,buffer,got,0)!=0;}
 BYTE digest[32];DWORD size=32;char actual[65]{};if(ok)ok=CryptGetHashParam(value,HP_HASHVAL,digest,&size,0)!=0;
 if(ok)for(int i=0;i<32;++i)std::sprintf(actual+i*2,"%02x",digest[i]);
 if(value)CryptDestroyHash(value);if(provider)CryptReleaseContext(provider,0);CloseHandle(file);return ok&&!std::strcmp(actual,wanted);
}
}
void note(const char* message){if(auto* f=std::fopen("left4craft.log","a")){std::fprintf(f,"[%llu] %s\n",static_cast<unsigned long long>(GetTickCount64()),message);std::fclose(f);}}
bool checkBuild(){return hash(GetModuleHandleW(L"engine.dll"),"ca17bce993672cfe89b9cb270ea5e28e5b590523e61a6b80f862a26312f4d2e0")&&hash(GetModuleHandleW(L"client.dll"),"7612545f68a6b670fb4d4a99a79c69f2d3939ebb80c15c1fd5c2b8c864f4daeb")&&hash(GetModuleHandleW(L"server.dll"),"b14de7055b123ff2f4ec50fd059118361cb4d601b976e39019b97bd205802265");}
void screenSize(int& w,int& h){w=1280;h=720;if(engineClient)method<void(__thiscall*)(void*,int&,int&)>(engineClient,5)(engineClient,w,h);}
void lookAngles(float angles[3]){angles[0]=angles[1]=angles[2]=0;if(engineClient)method<void(__thiscall*)(void*,float*)>(engineClient,19)(engineClient,angles);}
bool clientMenu(){return (engineVgui&&method<bool(__thiscall*)(void*)>(engineVgui,2)(engineVgui))||!engineClient||method<bool(__thiscall*)(void*)>(engineClient,11)(engineClient)||!method<bool(__thiscall*)(void*)>(engineClient,26)(engineClient);}
bool readPlayer(p::McState& state){
 if(!controlling||!hostLink||!hostLink->valid()||!hostLink->minecraftAlive()||!hostLink->mcState(state)||(state.flags&(p::kMcInWorld|p::kMcDead))!=p::kMcInWorld)return false;
 const double values[]={state.x,state.y,state.z,state.prevX,state.prevY,state.prevZ,state.curX,state.curY,state.curZ,state.eyeX,state.eyeY,state.eyeZ};for(double value:values)if(!std::isfinite(value)||std::fabs(value)>819.1){InterlockedExchange(&requested,0);return false;}
 // Use Source's frame clock rather than sampling Minecraft's last rendered frame.
 // A short history and 25 ms publication allowance keep interpolation continuous
 // when the two processes render at different phases.
 static MotionClock motion;
 LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
 motion.sample(state,now.QuadPart,frequency.QuadPart);
 return true;
}
void cursorPosition(float& x,float& y){x=cursorX;y=cursorY;}
void pollInput(){
 if(!hostLink)return;
 if(!gameWindow&&focus()){gameWindow=GetForegroundWindow();windowOriginal=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(gameWindow,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(windowHook)));}
 bool toggle=down(VK_F8);if(toggle&&!held[VK_F8]&&focus()){InterlockedExchange(&requested,!requested);note(requested?"F8: takeover requested":"F8: takeover disabled");}held[VK_F8]=toggle;
 if(down(VK_F9))InterlockedExchange(&requested,0);
 static bool active=false;bool next=controlling&&focus()&&!clientMenu();
 if(!next){if(active){hostLink->input(p::kInReleaseAll,0);for(int i=0;i<256;++i)if(i!=VK_F8)held[i]=false;}active=false;return;}active=true;
 for(int i=0;i<26;++i){if('A'+i=='O')continue;key('A'+i,4+i);}for(int i=0;i<9;++i)key('1'+i,30+i);key('0',39);
 key(VK_SPACE,44);key(VK_RETURN,40);key(VK_BACK,42);key(VK_TAB,43);key(VK_LSHIFT,225);key(VK_LCONTROL,224);key(VK_LMENU,226);
 bool menu=down('O');if(menu&&!held['O']){p::McState state{};if(readPlayer(state)&&(state.flags&p::kMcScreenOpen)){hostLink->input(p::kInKey,41,1);hostLink->input(p::kInKey,41,0);}else hostLink->input(p::kInOpenMenu,0);}held['O']=menu;
 const int buttons[]={VK_LBUTTON,VK_MBUTTON,VK_RBUTTON};for(int i=0;i<3;++i){bool value=down(buttons[i]);if(value!=held[buttons[i]]){held[buttons[i]]=value;hostLink->input(p::kInMouseButton,i+1,value);}}
}
bool installClient(){
 auto factory=[](HMODULE mod){return reinterpret_cast<void*(__cdecl*)(const char*,int*)>(GetProcAddress(mod,"CreateInterface"));};
 auto ef=factory(GetModuleHandleW(L"engine.dll")),cf=factory(GetModuleHandleW(L"client.dll"));if(!ef||!cf)return false;
 engineVgui=ef("VEngineVGui001",nullptr);engineClient=ef("VEngineClient013",nullptr);entities=cf("VClientEntityList003",nullptr);if(!engineClient||!entities||MH_Initialize()!=MH_OK)return false;
 auto client=reinterpret_cast<char*>(GetModuleHandleW(L"client.dll")),server=reinterpret_cast<char*>(GetModuleHandleW(L"server.dll"));
 bool ok=MH_CreateHook(client+0x219b80,reinterpret_cast<void*>(clientMoveHook),reinterpret_cast<void**>(&clientMove))==MH_OK
 &&MH_CreateHook(server+0x264480,reinterpret_cast<void*>(serverMoveHook),reinterpret_cast<void**>(&serverMove))==MH_OK
 &&MH_CreateHook(client+0xb40c0,reinterpret_cast<void*>(createHook),reinterpret_cast<void**>(&createOriginal))==MH_OK
 &&MH_CreateHook(client+0x213600,reinterpret_cast<void*>(viewHook),reinterpret_cast<void**>(&viewOriginal))==MH_OK
 &&MH_CreateHook(server+0x295e50,reinterpret_cast<void*>(damageHook),reinterpret_cast<void**>(&damageOriginal))==MH_OK
 &&MH_CreateHook(client+0x1d5b90,reinterpret_cast<void*>(drawViewmodelHook),reinterpret_cast<void**>(&drawViewmodelOriginal))==MH_OK
 &&MH_CreateHook(client+0xb40f0,reinterpret_cast<void*>(mouseHook),reinterpret_cast<void**>(&mouseOriginal))==MH_OK&&renderInstall();
 if(ok)ok=MH_EnableHook(MH_ALL_HOOKS)==MH_OK;
 if(!ok){MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();renderShutdown();}return ok;
}
void uninstallClient(){InterlockedExchange(&requested,0);InterlockedExchange(&controlling,0);if(gameWindow&&windowOriginal)SetWindowLongPtrW(gameWindow,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(windowOriginal));gameWindow=nullptr;windowOriginal=nullptr;MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();renderShutdown();hostLink=nullptr;serverPlayer=nullptr;}
}
