# HookCrashers Modding Guide

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
hookcrashers_version: ">=5.0.2"
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
hookcrashers_version: ">=5.0.2"
dependencies: []
entrypoints: []
```
