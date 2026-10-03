#pragma once
#include <cmath>
#include <cstdint>

namespace left4craft {
// L4D's standing survivor hull is 72 units tall. Minecraft's is 1.8 blocks.
// This is a prototype scale choice, not an assertion that a Source unit is a metre.
inline constexpr double unitsPerBlock = 40.0;
struct Point { double x, y, z; };
inline Point sourceToMc(Point p) { return {p.x/unitsPerBlock, p.z/unitsPerBlock, -p.y/unitsPerBlock}; }
inline Point mcToSource(Point p) { return {p.x*unitsPerBlock, -p.z*unitsPerBlock, p.y*unitsPerBlock}; }
inline float sourceYawToMc(float yaw) { return 270.0f-yaw; }
inline float mcYawToSource(float yaw) { return 270.0f-yaw; }
inline std::uint32_t mapId(const char* name) {
    std::uint32_t h=2166136261u;
    while (*name) { h ^= static_cast<unsigned char>(*name++); h *= 16777619u; }
    return h ? h : 1;
}
}
