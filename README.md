# HookCrashers

HookCrashers is an ASI modding platform for **Castle Crashers Remastered**.

The current goal is simple: addon character mods should be folder-based and runtime-driven. Mod authors should not have to edit or repack the game's `.pak` files for save layout, localization, lobby portraits, or character roster expansion.

HookCrashers currently provides:

- Lua folder mods from `mods/<mod>/`
- native x86 ASI folder mods from `mods/<mod>/*.asi`
- addon character registration
- runtime save layout expansion
- runtime `main.cok6` ActionScript patching for the save system
- runtime `lobby.cok6` portrait injection from per-character `portrait.swf` files
- custom localization from `localizations.json`
- multiplayer roster table patches for expanded addon slots
- a C++ API for advanced ASI mods and custom SWF native calls

## Installation

1. Copy `HookCrashers.asi` and its required runtime files into the Castle Crashers Remastered game folder.
2. Start the game once. HookCrashers will create `HookCrashers.yaml` and `mods` if they do not already exist.
3. Place folder mods under `mods/<mod>/`.

Example layout:

```text
CastleCrashers/
  HookCrashers.asi
  HookCrashers.yaml
  mods/
    example_mod/
      manifest.yaml
      localizations.json
      scripts/
        register_character.lua
      characters/
        troll/
          portrait.swf
          portrait_classic.swf
```

Back up your save data before testing character mods. Addon character order is part of the save mapping, so changing registration order after saves exist can remap addon slots.

## Configuration

`HookCrashers.yaml` is created in the game folder.

```yaml
updates_enabled: true
channel: stable
testing_key: ""
show_external_console: false
enable_overlay: true
overlay_toggle_key: Home
```

The ImGui log overlay starts closed. Press the configured `overlay_toggle_key` (`Home` by default) to show or hide it. `show_external_console` controls the separate Windows console window.

Save expansion, localization, and runtime SWF patching are driven by the loaded folder mods. Custom localization ids start at the fixed base id `5000`.

## Folder Mods

Each folder inside `mods` is scanned independently. A mod uses `manifest.yaml` to declare its metadata and Lua entrypoints.

### manifest.yaml

```yaml
id: example.character_pack
name: Example Character Pack
description: Example HookCrashers character mod.
author: YourName
version: 1.0.0
hookcrashers_version: ">=5.0.0"
dependencies: []
entrypoints:
  - scripts/register_character.lua
```

### Character registration

Every file listed under `entrypoints` is executed as Lua. Register characters in a stable order:

```lua
RegisterCharacter(
    "troll",
    Enums.Weapon.Pitchfork,
    Enums.Pet.None,
    true,
    false
)
```

`RegisterCharacter(id, weapon, pet, initiallyUnlocked, freshOnly)`:

- `id`: stable internal character id. Use lowercase ASCII names without spaces.
- `weapon`: starting weapon id. Prefer `HC.Enums.Weapon.*`.
- `pet`: starting animal orb id. Prefer `HC.Enums.Pet.*`.
- `initiallyUnlocked`: `true` if the character should start unlocked.
- `freshOnly`: `true` if this character is intended only for the fresh/custom character path. Use `false` when the UI should expose it in both classic and fresh/custom contexts.

See [docs/enums.md](docs/enums.md) for the complete weapon and pet enum list.

Registration order matters. HookCrashers assigns addon character ids from the Lua registration order. Every player in a multiplayer lobby must use the same mod set and registration order until mod manifest negotiation is implemented.

#### Save File Name

HookCrashers derives the modded save name automatically from the first loaded mod's manifest `name`. It removes `HookCrashers` case-insensitively, lowercases the result, and sanitizes it. For example, `name: HookCrashers Example Mod` uses:

```text
hc_examplemod.dat
hc_examplemod.dat.bak
```

The vanilla `cc_save.dat` and its backup are never used by a loaded mod. Only lowercase letters, numbers, `_`, and `-` are kept in the manifest name; invalid or empty names fall back to `mod`. Changing the manifest name intentionally creates a separate save namespace.

### Character Assets

HookCrashers checks the character folder for portrait assets:

```text
mods/example_mod/characters/troll/
  portrait.swf
  portrait_classic.swf
```

`portrait.swf` is the fresh portrait. `portrait_classic.swf` is the classic portrait.
Missing files produce a warning and that portrait is not injected; there is no fallback.

PNG and SVG experiments exist in code, but the supported path for current runtime portrait injection is `portrait.swf`.

## Runtime Save System Patch

The save-system SWF patch is automatic. Do not manually replace or edit `f_InitSaveSystem()` in `main.swf` anymore.

When `main.cok6` is loaded, HookCrashers patches the save initialization bytecode before the game parses the SWF. The patched runtime function reads the active save layout from HookCrashers, including character counts, addon counts, item ranges, animal ranges, level ranges, relic ranges, and the fixed `relic_offset` / `weapon_offset` values.

This keeps the SWF, save data, and runtime character table aligned without hardcoding new values into a repacked `main.pak`. The intended long-term workflow is that mods stay folder-based and HookCrashers handles the required runtime patching internally.

HookCrashers writes a debug dump of the patched SWF to:

```text
CastleCrashers/HookCrashersDumps/main_patched.swf
```

Use this dump only for inspection. The game consumes the patched buffer directly at runtime.

## Runtime Lobby Portrait Injection

Lobby portraits are also injected at runtime. The intended flow is:

1. A character registers in Lua.
2. HookCrashers finds `portrait.swf` for fresh mode and `portrait_classic.swf` for classic mode.
3. When `lobby.cok6` loads, HookCrashers extracts the first `DefineShape` from each available portrait SWF.
4. The imported shape is assigned a new character id after the vanilla lobby shapes.
5. Classic portraits replace the free timeline frames after frame 34 without shifting the later timeline.
6. Fresh portraits are inserted after frame 135, shifting every later frame once per injected portrait.
7. Each cloned frame keeps the vanilla transform/alignment and swaps only the placed character id to the imported portrait shape.

This means the mod portrait should inherit the same position, scale, and depth behavior as the vanilla portrait frame. The injected shape is appended at runtime; the original `.pak` stays untouched.

HookCrashers writes a debug dump of the patched lobby SWF to:

```text
CastleCrashers/HookCrashersDumps/lobby_patched.swf
```

### Portrait SWF Requirements

`portrait.swf` and `portrait_classic.swf` should be tiny uncompressed SWFs containing one portrait shape each.

Requirements:

- Use `FWS`, not `CWS` or `ZWS`.
- Include exactly one intended `DefineShape`/`DefineShape2`/`DefineShape3`/`DefineShape4` portrait shape.
- Put the portrait art in the same coordinate space and visual scale as the vanilla lobby portraits.
- Prefer `DefineShape3` when alpha/RGBA data is needed.
- Do not rely on frame scripts, sprites, buttons, or timeline behavior inside either file; HookCrashers imports the shape definition, not the whole movie.
- Keep one portrait per character folder. Shared portrait files are allowed, but shared paths will reuse the same injected definition.

For in-game sprites and animation assets, keep using `main.swf`/game asset authoring for now. The runtime portrait injector only handles lobby portrait shapes.

## Localization

`localizations.json` maps localization ids to translated strings. Supported language keys are `en`, `de`, `fr`, `it`, `ko`, `es`, `zh_cn`, `zh_tw`, `ru`, `ja`, and `pt`.

```json
{
  "en": {
    "characters.troll.name": "Troll",
    "characters.troll.unlock": "Unlocked by Example Character Pack."
  },
  "it": {
    "characters.troll.name": "Troll",
    "characters.troll.unlock": "Sbloccato da Example Character Pack."
  }
}
```

Character localization ids follow `characters.<registered id>.name` and `characters.<registered id>.unlock`. Missing translations fall back to English.

Use a custom localization from ActionScript exactly like the v4 API:

```actionscript
nameText.ntext = GetLocalization("characters.troll.name");
unlockText.ntext = GetLocalization("characters.troll.unlock");
```

`GetLocalization` returns the numeric localization id assigned by HookCrashers (starting at `5000`). Assign that value to `ntext`; use `text` only for literal strings.

## Multiplayer Notes

With addon characters loaded, HookCrashers uses expanded character synchronization
(HC protocol 6). Joining clients adopt the host's Classic/Fresh mode. Without
addons, the native synchronization path is retained.

- All modded players must use a compatible HookCrashers build (5.1.1 or 5.1.1-dev),
  the same folder mods and the same character registration order.
- Use Play Vanilla to open the native multiplayer flow; installed mods remain loaded.
- Play Modded - soon is disabled while the custom lobby browser is unfinished.
- Old HC protocol packets are rejected. Unmodified clients are not guaranteed
  compatible with addon characters.

## C++ ASI API

Advanced binary mods use the `HookCrashers-SDK-Win32.zip` asset published with each
HookCrashers release. It contains `sdk/include/HookCrashers.h` and the Win32/x86
import library `sdk/lib/HookCrashers.lib`, plus examples and a setup tutorial.

Native ASIs are folder mods. Do not place an ASI directly in the `mods` root:

```text
CastleCrashers/
  mods/
    MyNativeMod/
      manifest.yaml
      MyNativeMod.asi
```

HookCrashers loads every `.asi` in the mod folder in case-insensitive filename
order. The normal DLL `DllMain` runs automatically. A mod can also export the
optional initialization and shutdown entrypoints:

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

Build the mod as a **Win32/x86** DLL, change the output extension to `.asi`, add
the SDK include directory, and link `HookCrashers.lib`. Register characters
during `DllMain` or `HookCrashersModInit`; that is when HookCrashers associates
them with the native mod's `characters/<id>/` asset directory.

Character registration:

```cpp
#include "HookCrashers.h"

HookCrashers::RegisterCharacter(
    "troll",
    HookCrashers::Enums::Weapon::Pitchfork,
    HookCrashers::Enums::Pet::None,
    true,
    false);
```

The current stable C++ ABI exposes version lookup, logging, character
registration, and native confirmation prompts. Additional APIs will be added
without requiring mods to link against HookCrashers internals.

## Troubleshooting

- Check `HookCrashers.log` first. Lua syntax errors, registration errors, localization loads, addon character loads, network patch setup, and SWF patch failures are written there.
- If a mod folder is ignored, make sure it contains a valid `manifest.yaml` and that every declared entrypoint exists.
- If a native mod is ignored, put it under `mods/<mod-name>/<file>.asi`, compile it for Win32/x86, and check the `Native mod load failed` line for the exact Windows loader error.
- If the save screen behaves like vanilla, check that `main_patched.swf` is being dumped and that the log reports the `f_InitSaveSystem` runtime patch.
- If a portrait is missing, check that `portrait.swf` or `portrait_classic.swf` exists, is uncompressed `FWS`, and contains a real `DefineShape` tag.
- If a portrait appears in the wrong place, inspect `lobby_patched.swf` and confirm classic frames were cloned from frame 34 and fresh frames from frame 135.
- If multiplayer desyncs, first test with exactly one addon character and identical mod folders on both clients.

## Disclaimer

HookCrashers is intended for modding, research, and creative extension of Castle Crashers Remastered.

Do not use HookCrashers to build cheats, trainers, piracy tools, DLC unlockers, or anything designed to give an unfair multiplayer advantage. Mod responsibly and keep backups of original game files and saves.
