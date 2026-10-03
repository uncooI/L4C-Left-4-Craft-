#pragma once
#include "link.h"
#include "coordinates.h"
#include <d3d9.h>

namespace left4craft {
template<class Fn> Fn method(void* object,unsigned index) { return reinterpret_cast<Fn>((*reinterpret_cast<void***>(object))[index]); }
extern Link* hostLink;
extern void* engineClient;
extern volatile LONG requested,controlling;
extern void* serverPlayer;
void note(const char* message);
bool installClient();
void uninstallClient();
void pollInput();
void cursorPosition(float& x,float& y);
void screenSize(int& w,int& h);
void lookAngles(float angles[3]);
bool clientMenu();
bool readPlayer(p::McState& state);
bool renderInstall();
// Internal collision metadata; upstream Minecraft ignores this reserved high bit.
inline constexpr unsigned collisionNoVisual=1u<<31;
void renderClearCollision();
void renderCollisionRegion(const p::ColRegion& region,const p::ColTri* triangles);
void renderShutdown();
void renderFrame(IDirect3DDevice9* device);
bool checkBuild();
}
