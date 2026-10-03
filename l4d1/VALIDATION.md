# Validation — Left4Craft 0.2

Passed locally:

- C++20 protocol layout, shared-region boundaries, Source/Minecraft axes, scale,
  cardinal yaw directions, negative coordinates, round trips and map identifiers.
- All native host sources compiled as x86 **Microsoft C++ ABI** objects with Clang
  18, then linked with the MinGW Windows CRT/import libraries and C-only MinHook.
- PE32/i386 DLL export inspection: `CreateInterface`; no MinGW runtime DLL dependency.
- Supplied binaries inspected for interfaces, RTTI vtables and relevant code:
  client camera/input/movement, server movement/damage, engine camera matrices.
  CMoveData origin is explicitly written at verified offset 0x9c: the SDK lacks
  an intervening field and its GetAbsOrigin/SetAbsOrigin accessors cannot be used.
  Engine brush-plane export was also checked against the supplied binary.
- Built DLL checks SHA256 of engine.dll, client.dll and server.dll before hooks.
- Original upstream Fabric JAR inspected for protocol v11, mapping-property
  support and required dependencies. Packaged patch checked with `javap`: the
  linked FPS limit is now 60; its instruction size and wire layout are unchanged.
- Package checked for included launcher/mods, notices, scripts and source, with
  uploaded proprietary game binaries and repository internals excluded.

Not performed:

- Loading or playing inside L4D1: no installed game/runtime here.
- Minecraft account sign-in, first-run downloads or cross-process integration.
- Rendering/depth, collision correctness, campaign behavior or performance tests.
- Running the Windows PowerShell setup/launch scripts on Windows.
- A native Visual Studio build. The supplied DLL uses Microsoft ABI cross-builds.
- A complete modified Fabric build: Gradle failed to resolve Fabric Loom. The
  package instead uses the official release JAR plus a documented FPS-only patch.

The pinned SDK needed syntax-only changes locally: modern constructor spelling
in tier0/threadtools.h and explicit `word ptr` operands for three x86 `movzx`
assembly instructions in mathlib/mathlib.h. No interface declarations were changed.

Compilation and binary inspection do not prove runtime stability. This artifact
is a prototype to begin in-game testing, not a validated campaign-ready mod.
