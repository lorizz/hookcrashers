## C++ ASI mini tutorial — 5.1.0-dev

### 1. Get the SDK

Download **HookCrashers-SDK-Win32.zip** from this release and extract it.
It contains `sdk/include`, `sdk/lib/HookCrashers.lib`, the API reference in
`docs/current-character-api.md`, and buildable examples in `examples/native`.
Use the **HookCrashers.asi from the same release** in your Castle Crashers folder;
older builds do not export the character/update APIs. The SDK is not needed on
the player's machine. `HookCrashers.lib` is an import library for the ASI, not
a standalone copy of HookCrashers.

### 2. Build the working example

Install Visual Studio with **Desktop development with C++**, the MSVC C++ tools
and a Windows SDK. Open
`examples/native/CharacterLevelWatch/CharacterLevelWatch.vcxproj`, select
**Release | Win32**, then Build. If Visual Studio asks to retarget the compiler
or Windows SDK to your installed version, accept it. Castle Crashers is 32-bit:
an x64/ARM64 mod will not load, even on a 64-bit OS.

The result is `examples/native/CharacterLevelWatch/bin/Release/CharacterLevelWatch.asi`.
The project locates the extracted SDK automatically. Its source is
`examples/native/CharacterUpdate.cpp`; it provides a normal `void Update()` that
checks all four player slots and logs a level increase once.

### 3. Install your mod

Install the matching HookCrashers drop-in build, including its ASI loader.
Create this layout next to `castle.exe` (copy the example's `manifest.yaml`):

```text
CastleCrashers/
  castle.exe
  dinput8.dll
  HookCrashers.asi
  mods/
    CharacterLevelWatch/
      manifest.yaml
      CharacterLevelWatch.asi
```

Keep the mod inside its own folder, not directly in `mods/`. Restart the game
after rebuilding. Check `HookCrashers.log` for the mod-load entry and level-up
messages. No Lua script or addon character is required.

### 4. Write your Update()

The essential callback setup is:

```cpp
#include <HookCrashers.h>

void Update() {
    auto player = HookCrashers::GetCurrentCharacterData(1); // Slots 1..4
    // Check result, source, state and validFields before using a field.
    // Compare player.level with your previous valid reading here.
}
void __stdcall OnUpdate() { Update(); }

HOOKCRASHERS_MOD_EXPORT bool __stdcall HookCrashersModInit() {
    return HookCrashers::RegisterUpdateCallback(OnUpdate);
}
HOOKCRASHERS_MOD_EXPORT void __stdcall HookCrashersModShutdown() {
    HookCrashers::UnregisterUpdateCallback(OnUpdate);
}
```

`OnUpdate` runs on the game thread after the snapshots are updated. Keep it
short; queue achievement uploads or disk operations to a worker. Register in
ModInit and unregister in ModShutdown, not inside Update or DllMain. Before
unloading your own module, successful unregistration must finish outside the
Windows loader lock. HookCrashers handles the optional ModInit/ModShutdown
exports; no custom DllMain or thread loop is required.

For achievement checks require `result == Ok`, `source == LivePlayer`,
`state == InLevel`, and `HasCharacterField` for each field you read. An initial
reading is a baseline, not a level-up. Reset it when the player changes or data
becomes unavailable/loading. The full example implements these checks. Polling
cannot recover intermediate changes between snapshots or across loading gaps.

### 5. Start a fresh project (optional)

Create a **C++ Dynamic-Link Library** project. Use Win32, C++17 or later, and
change **Linker > General > Output File** to `$(OutDir)$(TargetName).asi`.
Add `sdk/include` to C/C++ additional include directories and `sdk/lib` to linker
additional library directories. Add `HookCrashers.lib` to linker input (MSVC
also auto-links it from the header). Do **not** define `HOOKCRASHERS_EXPORTS`
in your consuming mod. Copy the Init/Update/Shutdown setup and create a manifest:

```yaml
id: example.character_level_watch
name: Character Level Watch
author: YourName
version: 1.0.0
hookcrashers_version: ">=5.1.0-dev"
dependencies: []
entrypoints: []
```

The APIs passed offline native/SDK tests. The complete in-game player chain and
gameplay lifecycle still need tester validation in this dev release.
