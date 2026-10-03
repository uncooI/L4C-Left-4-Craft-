#include "skycraft_protocol.h"
#include "coordinates.h"
#include <cassert>
#include <cstddef>
#include <iostream>

int main() {
    namespace p=skycraft::proto;
    static_assert(offsetof(p::Header,skyrimHeartbeatMs)==0x10);
    static_assert(offsetof(p::Header,mcHeartbeatMs)==0x18);
    static_assert(offsetof(p::SkyState,posX)==0x10);
    static_assert(offsetof(p::SkyState,teleportSeq)==0x30);
    static_assert(offsetof(p::McState,teleportAck)==0x30);
    static_assert(p::kMappingBytes<0x80000000ull); // Must fit L4D1's 32-bit address space.
    static_assert(p::kOffInputRing+p::kInputRingDataOff+p::kInputRingEntries*sizeof(p::InputEvent)<=p::kOffActorTable);
    static_assert(p::kOffActorTable+sizeof(p::ActorTable)<=p::kOffEventRing);
    static_assert(p::kOffEventRing+p::kEventRingDataOff+p::kEventRingEntries*sizeof(p::McEvent)<=p::kOffWorldEntities);
    static_assert(p::kOffWorldEntities+sizeof(p::WorldEntities)<=p::kOffCollisionRing);
    using namespace left4craft;
    const auto east=sourceToMc({40,0,0});
    const auto north=sourceToMc({0,40,0});
    const auto up=sourceToMc({0,0,40});
    assert(east.x==1 && east.y==0 && east.z==0);
    assert(north.x==0 && north.y==0 && north.z==-1);
    assert(up.x==0 && up.y==1 && up.z==0);
    for (Point point : {Point{-1234.5,2345.25,-90},Point{0,0,0},Point{1,2,3}}) {
        auto back=mcToSource(sourceToMc(point));
        assert(std::abs(back.x-point.x)<1e-9);
        assert(std::abs(back.y-point.y)<1e-9);
        assert(std::abs(back.z-point.z)<1e-9);
    }
    // MC forward=(-sin yaw, 0, cos yaw), Source forward=(cos yaw,sin yaw,0).
    constexpr double pi=3.141592653589793;
    for (float yaw : {0.f,90.f,180.f,270.f,-90.f}) {
        const auto m=sourceYawToMc(yaw)*pi/180;
        auto f=mcToSource({-std::sin(m),0,std::cos(m)});
        assert(std::abs(f.x/unitsPerBlock-std::cos(yaw*pi/180))<1e-6);
        assert(std::abs(f.y/unitsPerBlock-std::sin(yaw*pi/180))<1e-6);
    }
    assert(mapId("l4d_hospital01_apartment")!=mapId("l4d_hospital02_subway"));
    assert(std::floor(sourceToMc({-1,0,0}).x)==-1); // Negative map coordinates.
    std::cout << "Protocol v" << p::kVersion << ": structure boundaries, axes, yaw and negative coordinates passed.\n";
}
