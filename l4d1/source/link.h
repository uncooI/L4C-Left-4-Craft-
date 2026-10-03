#pragma once
#include <windows.h>

#include <cstring>
#include <cstdint>
#include "skycraft_protocol.h"

namespace left4craft {
namespace p = skycraft::proto;
inline constexpr wchar_t mappingName[] = L"Local\\Left4Craft_v1";
template<class T> class AtomicRef {
    T& value;
public:
    explicit AtomicRef(T& v): value(v) {}
    T load() const {
        if constexpr(sizeof(T)==8) return T(InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(&value),0,0));
        else return T(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&value),0,0));
    }
    void store(T next) const {
        if constexpr(sizeof(T)==8) InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&value),LONG64(next));
        else InterlockedExchange(reinterpret_cast<volatile LONG*>(&value),LONG(next));
    }
    T exchange(T next) const {
        if constexpr(sizeof(T)==8) return T(InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&value),LONG64(next)));
        else return T(InterlockedExchange(reinterpret_cast<volatile LONG*>(&value),LONG(next)));
    }
};
template<class T> AtomicRef<T> atom(T& v) { return AtomicRef<T>(v); }

class Link {
    HANDLE mapping_{};
    std::uint8_t* data_{};
    bool owns_{};
    volatile LONG inputLock_{};
public:
    ~Link() { close(); }
    template<class T> T* at(std::uint64_t off) { return reinterpret_cast<T*>(data_+off); }
    bool open() {
        if (data_) return true;
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,
            DWORD(p::kMappingBytes>>32),DWORD(p::kMappingBytes),mappingName);
        if (!mapping_) return false;
        const auto existed=GetLastError()==ERROR_ALREADY_EXISTS;
        data_=static_cast<std::uint8_t*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,0));
        if (!data_) { close(); return false; }
        // A surviving Minecraft process can retain an old mapping. Do not reset live data.
        if (existed) { close(); return false; }
        owns_=true;
        // Fresh paging-file mappings are zero-filled. Publish magic last.
        auto* h=at<p::Header>(p::kOffHeader);
        h->version=p::kVersion; h->skyrimPid=GetCurrentProcessId();
        atom(h->skyrimHeartbeatMs).store(GetTickCount64());
        atom(h->magic).store(p::kMagic);
        return true;
    }
    void close() {
        if (data_) {
            if (owns_) atom(at<p::Header>(0)->skyrimHeartbeatMs).store(0);
            UnmapViewOfFile(data_); data_=nullptr;
        }
        if (mapping_) { CloseHandle(mapping_); mapping_=nullptr; }
        owns_=false;
    }
    bool valid() const { return data_!=nullptr; }
    void heartbeat() { atom(at<p::Header>(0)->skyrimHeartbeatMs).store(GetTickCount64()); }
    bool minecraftAlive() {
        auto beat=atom(at<p::Header>(0)->mcHeartbeatMs).load();
        return beat && GetTickCount64()-beat<3000;
    }
    void state(const p::SkyState& value) {
        auto* dst=at<p::SkyState>(p::kOffSkyState);
        auto seq=atom(dst->seq); auto n=seq.load();
        seq.store(n+1);
        MemoryBarrier();
        std::memcpy(reinterpret_cast<char*>(dst)+4,reinterpret_cast<const char*>(&value)+4,sizeof(value)-4);
        seq.store(n+2);
    }
    bool collision(p::ColType type,const void* payload,std::uint32_t bytes) {
        auto* r=at<std::uint8_t>(p::kOffCollisionRing);
        auto& h=*reinterpret_cast<std::uint64_t*>(r+p::kColRingHeadOff);
        auto& t=*reinterpret_cast<std::uint64_t*>(r+p::kColRingTailOff);
        constexpr auto cap=p::kColRingDataBytes;
        auto head=atom(h).load();
        auto tail=atom(t).load();
        const auto n=(sizeof(p::ColMsgHeader)+std::uint64_t(bytes)+7)&~7ull;
        if (n>cap/2 || tail>head || head-tail>cap) return false;
        auto pos=head%cap;
        const auto pad=pos+n>cap ? cap-pos : 0;
        if (cap-(head-tail)<n+pad) return false;
        auto* buf=r+p::kColRingDataOff;
        if (pad) { *reinterpret_cast<p::ColMsgHeader*>(buf+pos)={p::kColPad,0}; head+=pad; pos=0; }
        *reinterpret_cast<p::ColMsgHeader*>(buf+pos)={std::uint32_t(type),bytes};
        if (bytes) std::memcpy(buf+pos+sizeof(p::ColMsgHeader),payload,bytes);
        atom(h).store(head+n);
        return true;
    }
    bool mcState(p::McState& out) {
        auto* src=at<p::McState>(p::kOffMcState);
        for(int n=0;n<16;++n) {
            auto seq=atom(src->seq).load(); if(seq&1) continue;
            MemoryBarrier(); std::memcpy(&out,src,sizeof(out)); MemoryBarrier();
            if(seq==atom(src->seq).load()) return true;
        }
        return false;
    }
    bool worldEntities(p::WorldEntities& out) {
        auto* src=at<p::WorldEntities>(p::kOffWorldEntities);
        for(int n=0;n<4;++n){auto seq=atom(src->seq).load();if(seq&1)continue;
            MemoryBarrier();std::memcpy(&out,src,sizeof(out));MemoryBarrier();
            if(seq==atom(src->seq).load())return out.count<=p::kMaxWorldEntities;
        }return false;
    }
    bool input(std::uint16_t type,std::uint16_t code,int a=0,int b=0,int c=0) {
        if(InterlockedCompareExchange(&inputLock_,1,0))return false;
        auto* r=at<std::uint8_t>(p::kOffInputRing);
        auto& h=*reinterpret_cast<std::uint64_t*>(r); auto& t=*reinterpret_cast<std::uint64_t*>(r+0x40);
        auto head=atom(h).load(),tail=atom(t).load();
        if(tail>head || head-tail>=p::kInputRingEntries){InterlockedExchange(&inputLock_,0);return false;}
        reinterpret_cast<p::InputEvent*>(r+0x80)[head&(p::kInputRingEntries-1)]={type,code,a,b,c};
        atom(h).store(head+1);InterlockedExchange(&inputLock_,0); return true;
    }
    bool event(p::McEvent& out) {
        auto* r=at<std::uint8_t>(p::kOffEventRing);
        auto& h=*reinterpret_cast<std::uint64_t*>(r); auto& t=*reinterpret_cast<std::uint64_t*>(r+0x40);
        auto head=atom(h).load(),tail=atom(t).load();
        if(tail>=head || head-tail>p::kEventRingEntries) return false;
        out=reinterpret_cast<p::McEvent*>(r+0x80)[tail&(p::kEventRingEntries-1)];
        atom(t).store(tail+1); return true;
    }
    void actors(const p::ActorRecord* src,unsigned count) {
        auto* dst=at<p::ActorTable>(p::kOffActorTable);auto seq=atom(dst->seq).load();
        if(count>p::kMaxActors) count=p::kMaxActors;
        atom(dst->seq).store(seq+1); MemoryBarrier();
        dst->count=count; std::memcpy(dst->actors,src,count*sizeof(*src)); atom(dst->seq).store(seq+2);
    }
    template<class Callback> void render(Callback consume,unsigned budget=8<<20) {
        auto* r=at<std::uint8_t>(p::kOffRenderRing);
        auto& h=*reinterpret_cast<std::uint64_t*>(r);auto& t=*reinterpret_cast<std::uint64_t*>(r+0x40);
        auto head=atom(h).load(),tail=atom(t).load(); constexpr auto cap=p::kRenRingDataBytes;
        if(tail>head || head-tail>cap) return;
        unsigned used=0;
        while(tail<head && used<budget) {
            auto pos=tail%cap; if(cap-pos<8) break;
            auto msg=*reinterpret_cast<p::ColMsgHeader*>(r+0x80+pos);
            if(msg.type==p::kRenPad) {tail+=cap-pos;continue;}
            auto size=(8ull+msg.payloadBytes+7)&~7ull;
            if(size>cap/2 || size>cap-pos || size>head-tail) break;
            consume(msg.type,r+0x80+pos+8,msg.payloadBytes);
            tail+=size; used+=msg.payloadBytes;
        }
        atom(t).store(tail);
    }
    unsigned overlayFront=2;
    const std::uint8_t* overlay(p::OverlaySlotHdr& out) {
        auto* ctl=at<p::OverlayCtl>(p::kOffOverlayCtl);
        if(atom(ctl->state).load()&p::kOverlayDirty) overlayFront=atom(ctl->state).exchange(overlayFront)&3;
        if(overlayFront>=p::kOverlaySlots) return nullptr;
        out=*at<p::OverlaySlotHdr>(p::kOffOverlaySlotHdr+overlayFront*0x40);
        if(!out.width || !out.height || out.width>p::kMaxOverlayW || out.height>p::kMaxOverlayH) return nullptr;
        return at<std::uint8_t>(p::kOffOverlayPixels+overlayFront*p::kOverlaySlotBytes);
    }
};
}
