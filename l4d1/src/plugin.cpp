// Experimental offline L4D1 host for the upstream SkyCraft Fabric mod.
#include "engine/iserverplugin.h"
#include "eiface.h"
#include "game/server/iplayerinfo.h"
#include "engine/IEngineTrace.h"
#include "tier0/valve_off.h"
#include "link.h"
#include "coordinates.h"
#include "integration.h"
#include <cmath>
#include "collision.h"
#include "iserverunknown.h"
#include "engine/ICollideable.h"
#include "basehandle.h"
#include <shellapi.h>
#include <cstdio>
#include <cmath>
#include <cwchar>

#undef GetClassName
namespace left4craft {
bool insecureLaunch() {
    int count=0; auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    if (!args) return false;
    bool found=false;
    for (int i=1;i<count;++i) if (!_wcsicmp(args[i],L"-insecure")) found=true;
    LocalFree(args); return found;
}

class Plugin final : public IServerPluginCallbacks {
    IVEngineServer* engine_{};
    IPlayerInfoManager* players_{};
    IEngineTrace* trace_{};
    edict_t* edicts_{};
    int clientMax_{};
    Link link_;
    p::SkyState state_{};
    bool paused_{};
    bool hadPlayer_{};
    std::uint32_t mcPid_{};
    int entityMax_{};
    unsigned localHandle_{};
    Point lastTeleport_{};
    Collision collision_;
    std::uint64_t actorAt_{};
    p::ActorRecord actors_[p::kMaxActors]{};
    unsigned actorCount_{};
    void log(const char* msg) { note(msg); }
    void exportActors() {
        actorCount_=0;
        for(int i=1;i<entityMax_&&actorCount_<p::kMaxActors;++i) {
            auto& e=edicts_[i];if(e.IsFree())continue;
            const char* name=e.GetClassName();bool hostile=name&&!std::strcmp(name,"infected");
            if(i<=clientMax_){auto* info=players_->GetPlayerInfo(&e);hostile=info&&info->GetTeamIndex()==3;}
            if(!hostile)continue;auto* unknown=e.GetUnknown();if(!unknown)continue;
            auto* base=unknown->GetBaseEntity();auto* collider=unknown->GetCollideable();if(!base||!collider)continue;
            int hp=*reinterpret_cast<int*>(reinterpret_cast<char*>(base)+0xd4);if(hp<=0)continue;
            auto& origin=collider->GetCollisionOrigin();auto mc=sourceToMc({origin.x,origin.y,origin.z});
            double dx=mc.x-state_.posX,dy=mc.y-state_.posY,dz=mc.z-state_.posZ;if(dx*dx+dy*dy+dz*dz>4096)continue;
            auto& a=actors_[actorCount_++];a={};a.formId=unknown->GetRefEHandle().ToInt();a.flags=p::kActorHostile|p::kActorInCombat;
            a.x=float(mc.x);a.y=float(mc.y);a.z=float(mc.z);a.yaw=sourceYawToMc(collider->GetCollisionAngles().y);
            a.width=(collider->OBBMaxs().x-collider->OBBMins().x)/40.f;a.height=(collider->OBBMaxs().z-collider->OBBMins().z)/40.f;
            int max=*reinterpret_cast<int*>(reinterpret_cast<char*>(base)+0xd8);a.healthFrac=max>0?float(hp)/max:1.f;a.level=1;std::strncpy(a.name,"Infected",sizeof(a.name)-1);
        }
        link_.actors(actors_,actorCount_);
    }
    void events() {
        p::McEvent event{};for(int n=0;n<256&&link_.event(event);++n){
            if(!controlling)continue;
            if(event.type==p::kEvPlayerDied){InterlockedExchange(&requested,0);note("Minecraft death: returning L4D control; campaign death synchronization is not implemented.");continue;}
            if(event.type!=p::kEvHitActor||!std::isfinite(event.a)||event.a<=0||event.a>2000)continue;
            bool mirrored=false;for(unsigned i=0;i<actorCount_;++i)if(actors_[i].formId==event.formId)mirrored=true;if(!mirrored)continue;
            unsigned index=event.formId&0xfff;if(index>=unsigned(entityMax_)||edicts_[index].IsFree())continue;
            auto* u=edicts_[index].GetUnknown();if(!u||u->GetRefEHandle().ToInt()!=event.formId)continue;auto* base=u->GetBaseEntity();if(!base)continue;
            // L4D1 damage layout differs from L4D2: no weapon EHANDLE.
            alignas(8) unsigned char info[128]{};
            auto playerHandle=localHandle_;
            *reinterpret_cast<unsigned*>(info+0x24)=playerHandle;*reinterpret_cast<unsigned*>(info+0x28)=playerHandle;
            *reinterpret_cast<float*>(info+0x2c)=event.a*5.f;*reinterpret_cast<float*>(info+0x30)=event.a*5.f;*reinterpret_cast<float*>(info+0x34)=event.a*5.f;
            *reinterpret_cast<int*>(info+0x38)=(event.flags&p::kHitProjectile)?2:128;*reinterpret_cast<int*>(info+0x44)=-1;
            method<int(__thiscall*)(void*,const void*)>(base,62)(base,info);
        }
    }
public:
    bool Load(CreateInterfaceFn engineFactory,CreateInterfaceFn serverFactory) override {
        if (!insecureLaunch()) { log("Refusing load: launch L4D1 with -insecure. -novid only skips videos."); return false; }
        engine_=static_cast<IVEngineServer*>(engineFactory(INTERFACEVERSION_VENGINESERVER,nullptr));
        players_=static_cast<IPlayerInfoManager*>(serverFactory(INTERFACEVERSION_PLAYERINFOMANAGER,nullptr));
        trace_=static_cast<IEngineTrace*>(engineFactory(INTERFACEVERSION_ENGINETRACE_SERVER,nullptr));
        if (!engine_ || !players_ || !trace_) { log("Required L4D1 SDK interface unavailable; nothing patched."); return false; }
        if (engine_->IsDedicatedServer()) { log("Listen-server/single-player only."); return false; }
        if (!link_.open()) { log("Could not create mapping; close any previous Left4Craft Minecraft first."); return false; }
        if(!checkBuild()){log("Unsupported engine/client/server hashes; no hooks installed.");link_.close();return false;}
        hostLink=&link_;
        if(!installClient()){log("Hook setup failed; no takeover available.");hostLink=nullptr;link_.close();return false;}
        state_.flags=p::kSkyLoading|p::kSkyMenuOpen;
        state_.viewportW=1280; state_.viewportH=720; state_.gameHour=12;
        link_.state(state_); log("Left4Craft 0.2 experimental loaded. F8 requests takeover, F9 returns native control."); return true;
    }
    void Unload() override { uninstallClient(); link_.close(); edicts_=nullptr; log("Unloaded."); }
    void Pause() override { paused_=true; }
    void UnPause() override { paused_=false; }
    const char* GetPluginDescription() override { return "Left4Craft 0.2 experimental offline prototype"; }
    void LevelInit(const char* map) override {
        state_.worldId=mapId(map); ++state_.collisionEpoch; ++state_.teleportSeq;
        state_.flags=p::kSkyLoading|p::kSkyMenuOpen; hadPlayer_=false; collision_.reset();
    }
    void ServerActivate(edict_t* list,int count,int maxClients) override { edicts_=list; entityMax_=count; clientMax_=maxClients; }
    void GameFrame(bool simulating) override {
        if (!link_.valid()) return;
        link_.heartbeat(); pollInput();
        IPlayerInfo* local=nullptr; edict_t* localEdict=nullptr; int humans=0;
        if (edicts_ && simulating && !paused_) for (int i=1;i<=clientMax_;++i) {
            if (edicts_[i].IsFree()) continue;
            auto* info=players_->GetPlayerInfo(&edicts_[i]);
            if (info && info->IsConnected() && !info->IsFakeClient() && !info->IsHLTV()) { ++humans; local=info; localEdict=&edicts_[i]; }
        }
        if (humans!=1 || !local || local->IsDead() || local->IsObserver()) {
            InterlockedExchange(&controlling,0); state_.flags=p::kSkyMenuOpen|p::kSkyLoading; link_.state(state_); return;
        }
        auto* unknown=localEdict->GetUnknown();if(!unknown||!unknown->GetCollideable())return;
        serverPlayer=unknown->GetBaseEntity();localHandle_=unknown->GetRefEHandle().ToInt();
        const auto& origin=unknown->GetCollideable()->GetCollisionOrigin();float look[3];lookAngles(look);
        const auto mc=sourceToMc({origin.x,origin.y,origin.z});
        const auto pid=atom(link_.at<p::Header>(0)->mcPid).load();
        if(!hadPlayer_||pid!=mcPid_){++state_.collisionEpoch;++state_.teleportSeq;collision_.reset();link_.collision(p::kColClear,&state_.collisionEpoch,4);mcPid_=pid;lastTeleport_=mc;}
        p::McState minecraft{};bool alive=link_.minecraftAlive();bool reading=alive&&link_.mcState(minecraft);
        bool enabled=requested&&alive&&collision_.ready&&reading&&minecraft.teleportAck==state_.teleportSeq&&(minecraft.flags&p::kMcInWorld)&&!(minecraft.flags&p::kMcDead);
        if(!enabled&&hadPlayer_){double dx=mc.x-lastTeleport_.x,dy=mc.y-lastTeleport_.y,dz=mc.z-lastTeleport_.z;if(dx*dx+dy*dy+dz*dz>0.0625){++state_.teleportSeq;lastTeleport_=mc;}}
        InterlockedExchange(&controlling,enabled);
        state_.flags=p::kSkyInGame;if(!enabled||clientMenu())state_.flags|=p::kSkyMenuOpen;
        state_.posX=mc.x; state_.posY=mc.y; state_.posZ=mc.z;
        state_.yaw=sourceYawToMc(look[1]); state_.pitch=look[0];
        int w,h;screenSize(w,h);state_.viewportW=w;state_.viewportH=h;
        hadPlayer_=true;link_.state(state_);
        if(alive){collision_.step(trace_,link_,mc,state_.collisionEpoch);if(GetTickCount64()-actorAt_>50){exportActors();actorAt_=GetTickCount64();}events();}

    }
    void LevelShutdown() override {
        InterlockedExchange(&controlling,0);InterlockedExchange(&requested,0);serverPlayer=nullptr;edicts_=nullptr; hadPlayer_=false; state_.flags=p::kSkyLoading|p::kSkyMenuOpen;
        if (link_.valid()) link_.state(state_);
    }
    void ClientActive(edict_t*) override {}
    void ClientDisconnect(edict_t*) override {}
    void ClientPutInServer(edict_t*,const char*) override {}
    void SetCommandClient(int) override {}
    void ClientSettingsChanged(edict_t*) override {}
    PLUGIN_RESULT ClientConnect(bool*,edict_t*,const char*,const char*,char*,int) override { return PLUGIN_CONTINUE; }
    PLUGIN_RESULT ClientCommand(edict_t*,const CCommand&) override { return PLUGIN_CONTINUE; }
    PLUGIN_RESULT NetworkIDValidated(const char*,const char*) override { return PLUGIN_CONTINUE; }
    void OnQueryCvarValueFinished(QueryCvarCookie_t,edict_t*,EQueryCvarValueStatus,const char*,const char*) override {}
};
Plugin plugin;
}
extern "C" __declspec(dllexport) void* CreateInterface(const char* name,int* code) {
    if (!std::strcmp(name,INTERFACEVERSION_ISERVERPLUGINCALLBACKS)) { if (code) *code=0; return &left4craft::plugin; }
    if (code) *code=1; return nullptr;
}
