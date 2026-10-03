# Left4Craft 0.2 — experimental L4D1 port of SkyCraft

A Windows x86 prototype for **Left 4 Dead 1, offline single player**. This uses
SkyCraft's actual Minecraft mod and shared-memory protocol, with a new Source
engine host. It is not a finished port. The DLL builds, but it has **not been
run inside L4D1 here**. Rendering, collision and combat need in-game verification.

## Try it

1. Extract the entire package to a writable folder. Close L4D1 and Minecraft.
2. Double-click `Setup.cmd` and select your L4D1 `left4dead.exe`. Setup checks the
   supplied game build and copies the plugin into `left4dead/addons`.
3. In the included Prism Launcher, add your Minecraft Java account and launch
   the **Left4Craft** instance once. Let it download Java/game files, then close
   Minecraft. This initial setup requires internet and a Minecraft Java account.
4. Put Steam into Offline Mode. Double-click `Launch.cmd`. It launches L4D1's
   No Mercy apartment map as single player and requests an offline Minecraft
   session. Allow Minecraft startup and collision streaming to finish.
5. Close the L4D console, then press **F8** to request Minecraft control.
   **F9** returns native L4D control. If no takeover happens, inspect
   `left4craft.log` in the L4D1 installation folder and Prism's Minecraft logs.

The launch options include `-insecure -novid`. **`-insecure` disables VAC;
`-novid` skips videos.** There is no automatic plugin-loading VDF: starting L4D1
normally does not automatically load this package. Setup leaves the game EXE,
engine/client/server DLLs and your game configuration files alone.

## Controls while takeover is active

| Control | Action |
| --- | --- |
| F8 / F9 | Request or toggle takeover / return native control |
| WASD, Space, left Shift, left Ctrl | Minecraft movement, jump, sneak, sprint |
| Left / right mouse | Minecraft attack/break / use/place |
| Wheel, 1–9 | Minecraft hotbar |
| E | Minecraft inventory |
| O | Minecraft pause/options menu; close the current Minecraft screen |
| G | Native L4D `+use` for doors and world interactions |
| Escape | Native L4D menu |

Minecraft GUI cursor movement is forwarded from mouse deltas. The native L4D
HUD and weapon are still visible; for a visual test, the L4D console commands
`cl_drawhud 0` and `r_drawviewmodel 0` can hide them. Set each back to `1` to
restore the usual display.

## Implemented code

- Source server plugin callbacks and protocol-v11 shared memory under
  `Local\Left4Craft_v1`, with heartbeats and seqlocks.
- Hash-gated hooks for the exact engine/client/server binaries supplied for this
  project. Unsupported versions refuse hooks. Dedicated servers are refused;
  takeover requires exactly one human player.
- D3D9 block atlas/animated atlas regions, chunk mesh rendering and transparent
  Minecraft HUD overlay, with render-state restoration and device-reset handling.
- Minecraft feet movement in local client/server movement routines and its eye
  position in the Source camera. Native Source mouse look drives Minecraft look.
- Keyboard, mouse, wheel, cursor and basic text forwarding.
- Budgeted, clipped BSP-brush collision triangles over nearby 8-block regions.
  Initial takeover waits until those regions have been exported.
- Nearby common/special infected mirrored as hittable Minecraft proxies. Minecraft
  hit events call native L4D damage handling after checking full entity handles.
  Incoming local-player L4D damage is forwarded to Minecraft while takeover is active.

## Known unfinished parts

- No displacement terrain, moving-door/prop geometry or water collision export.
  Some map surfaces therefore cannot support Minecraft movement yet.
- Minecraft-placed blocks do not alter Source AI navigation or collide with bots
  and infected. BSP digging/destruction is not implemented.
- Minecraft hand/viewmodel, avatars, dropped items, arrows and additional scene
  streams are not rendered by this host yet. The block meshes and HUD are the
  current rendering milestone.
- Campaign death, respawn, incapacitation, revives and special-infected grabs are
  not synchronized. Minecraft death ends takeover; this milestone needs a fresh
  session after death. It is not ready for a full campaign playthrough.
- Exact depth occlusion, camera timing, hook ABI behavior, input and performance
  remain unverified in the running game. Unsupported/malformed data is bounded
  and checked, but compilation does not establish runtime correctness.

The package reserves a large shared-memory mapping plus both games' memory.
The included Minecraft profile uses a 2 GB maximum heap and the mod's linked FPS
cap is reduced from 260 to 60 for the target 8 GB laptop. Actual performance has
not been measured.

## Remove it / source builds

Close L4D1, then run `Uninstall.cmd`. It removes this package's installed DLL and
restores a previous DLL backup if Setup made one. Minecraft saves remain under
`Minecraft/Prism/instances/Left4Craft/.minecraft`.

Windows development: Visual Studio C++ tools, CMake and Git, then
`powershell -File scripts/build.ps1`. Use **Win32 and Microsoft's C++ ABI**, not
GNU C++ ABI; their destructor vtable layouts differ. See `VALIDATION.md` and
`BUILD-PROVENANCE.json` for build pins and actual checks.

The packaged Fabric JAR comes from upstream SkyCraft 0.1.2, with a same-length
bytecode FPS-cap patch documented by `tools/patch_fabric.py`. The mapping override
is supplied in the Prism profile. A full modified Fabric source build did not
complete here because Loom plugin resolution failed. Sources are included.

Upstream: https://github.com/chasmlol/SkyCraft (MIT). The included launcher and
Fabric API are the upstream release's portable bundle, with notices preserved.
The optional e4mc public networking mod is omitted. No L4D1 game binaries,
Minecraft game files, accounts or credentials are included.
