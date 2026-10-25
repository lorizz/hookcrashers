# HookCrashers Modding Guide

For C++ runtime checks, start with the [ASI mini tutorial](QUICKSTART-ASI.md).
The repository includes the Win32 import library at `sdk/lib/HookCrashers.lib`.
Releases provide `HookCrashers-SDK-Win32.zip` with matching headers, library,
reference and Visual Studio examples.

HookCrashers supports both Lua folder mods and native Win32/x86 ASI mods. Every
mod lives in its own directory under `CastleCrashers/mods` and should include a
`manifest.yaml`.

## Lua mods

Lua is the recommended option for character packs and mods that only need the
runtime APIs exposed by HookCrashers.

```text
CastleCrashers/
  mods/
    MyLuaMod/
      manifest.yaml
      localizations.json
      scripts/
        main.lua
      characters/
        my_character/
          portrait.swf
          portrait_classic.swf
```

Example `manifest.yaml`:

```yaml
id: example.lua_mod
name: Example Lua Mod
author: YourName
version: 1.0.0
hookcrashers_version: ">=5.0.3"
dependencies: []
entrypoints:
  - scripts/main.lua
```

Example `scripts/main.lua`:

```lua
RegisterCharacter(
    "my_character",
    HC.Enums.Weapon.Pitchfork,
    HC.Enums.Pet.None,
    true,
    false
)
```

`RegisterCharacter(name, weapon, pet, initiallyUnlocked, freshOnly)` assigns
addon character ids in registration order. Multiplayer users must use the same
mods and registration order.

## C++ ASI mods

Build native mods as **Win32/x86** DLLs with an `.asi` extension.

1. Add `include` from this package to the compiler include path.
2. Link `HookCrashers.lib` from the SDK package.
3. Place the compiled ASI inside its own folder under `mods`.

```text
CastleCrashers/
  HookCrashers.asi
  mods/
    MyNativeMod/
      manifest.yaml
      MyNativeMod.asi
```

Loading the ASI runs its normal `DllMain`. A mod may additionally export
`HookCrashersModInit` and `HookCrashersModShutdown`. When present, HookCrashers
calls them after loading and before an explicit unload.
For runtime checks, register a `void __stdcall OnUpdate()` using
`HookCrashers::RegisterUpdateCallback` in ModInit and unregister it in ModShutdown.
It runs on the game thread after character snapshots are published. See
`examples/native/CharacterUpdate.cpp` and `docs/current-character-api.md` for
the level-increase example and callback lifetime/threading contract.
Register characters during `DllMain` or `HookCrashersModInit` so HookCrashers can
associate them with the native mod's `characters/<id>/` asset directory.

```cpp
#include <HookCrashers.h>

HOOKCRASHERS_MOD_EXPORT bool __stdcall HookCrashersModInit() {
    HookCrashers::LogInfo("MyNativeMod initialized");
    return true;
}

HOOKCRASHERS_MOD_EXPORT void __stdcall HookCrashersModShutdown() {
    HookCrashers::LogInfo("MyNativeMod shutting down");
}
```

The mod folder must contain a `manifest.yaml`, even when it has no Lua scripts:

```yaml
id: example.native_mod
name: Example Native Mod
author: YourName
version: 1.0.0
hookcrashers_version: ">=5.0.3"
dependencies: []
entrypoints: []
```

## Current character data (5.1.0-dev)

`HookCrashers::GetCurrentCharacterData(playerSlot = 1)` returns a versioned copy
of live player data or explicitly marked HUD data. Check `result`, `source`,
`state` and each numeric field's validity bit. Generic property enumeration also
supports addon fields and bounded arrays/nested objects. See
[the API reference](../docs/current-character-api.md) for examples, ABI, limits,
threading and the remaining live-game validation requirement.

## HookCrashers 5.0.3 changelog

- Added native Win32/x86 ASI loading from dedicated `mods/<mod>/*.asi`
  folders.
- Added deterministic native-mod load order.
- Added optional `HookCrashersModInit` and `HookCrashersModShutdown` exports.
- Added `HookCrashers.h` and the x86 `HookCrashers.lib` import library.
- Added public logging and HookCrashers runtime-version APIs for C++ mods.
- Added a buildable native example and corrected the C++ API documentation.
