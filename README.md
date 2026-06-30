# HookCrashers

HookCrashers is an ASI modding platform for **Castle Crashers Remastered**.

The current goal is simple: addon character mods should be folder-based and runtime-driven. Mod authors should not have to edit or repack the game's `.pak` files for save layout, localization, lobby portraits, or character roster expansion.

HookCrashers currently provides:

- Lua folder mods from `mods/<mod>/`
- addon character registration
- runtime save layout expansion
- runtime `main.cok6` ActionScript patching for the save system
- runtime `lobby.cok6` portrait injection from per-character `portrait.swf` files
- custom localization from `locs.json`
- multiplayer roster table patches for expanded addon slots
- a C++ API for advanced ASI mods and custom SWF native calls

## Installation

1. Copy `HookCrashers.asi` and its required runtime files into the Castle Crashers Remastered game folder.
2. Start the game once. HookCrashers will create `HookCrashers.ini` and `mods` if they do not already exist.
3. Place folder mods under `mods/<mod>/`.

Example layout:

```text
CastleCrashers/
  HookCrashers.asi
  HookCrashers.ini
  mods/
    example_mod/
      manifest.json
      main.lua
      locs.json
      characters/
        troll/
          portrait.swf
```

Back up your save data before testing character mods. Addon character order is part of the save mapping, so changing registration order after saves exist can remap addon slots.

## Configuration

`HookCrashers.ini` is created in the game folder.

```ini
[HookCrashers]
ShowExternalConsole=false
EnableOverlay=true
OverlayToggleKey=Home
```

Save expansion, localization, and runtime SWF patching are driven by the loaded folder mods. Custom localization ids start at the fixed base id `5000`.

## Folder Mods

Each folder inside `mods` is scanned independently. A folder is considered a mod when it contains at least one supported file: `manifest.json`, `main.lua`, `locs.json`, or `icon.png`.

### manifest.json

```json
{
  "name": "Example Character Pack",
  "author": "YourName",
  "version": "1.0.0"
}
```

### main.lua

`main.lua` is executed with the `HookCrashers` Lua module available.

```lua
local HC = require("HookCrashers")

HC.Character.Register(
    "troll",
    HC.Enums.Weapon.Pitchfork,
    HC.Enums.Pet.None,
    true,
    false
)
```

The same function is also exposed as `HC.RegisterCharacter(...)`.

`HC.Character.Register(id, weapon, pet, initiallyUnlocked, freshOnly)`:

- `id`: stable internal character id. Use lowercase ASCII names without spaces.
- `weapon`: starting weapon id. Prefer `HC.Enums.Weapon.*`.
- `pet`: starting animal orb id. Prefer `HC.Enums.Pet.*`.
- `initiallyUnlocked`: `true` if the character should start unlocked.
- `freshOnly`: `true` if this character is intended only for the fresh/custom character path. Use `false` when the UI should expose it in both classic and fresh/custom contexts.

See [docs/enums.md](docs/enums.md) for the complete weapon and pet enum list.

Registration order matters. HookCrashers assigns addon character ids from the Lua registration order. Every player in a multiplayer lobby must use the same mod set and registration order until mod manifest negotiation is implemented.

#### Save File Name

HookCrashers redirects Castle Crashers saves away from the vanilla `cc_save.dat`. By default it uses:

```text
cc_save_mod.dat
cc_save_mod.dat.bak
```

Use `RegisterSaveName` before registering characters when a mod needs its own save file:

```lua
local HC = require("HookCrashers")

HC.RegisterSaveName("my_mod")
-- or: HC.Save.RegisterName("my_mod")

HC.Character.Register(
    "troll",
    HC.Enums.Weapon.Pitchfork,
    HC.Enums.Pet.None,
    true,
    false
)
```

`HC.RegisterSaveName("my_mod")` uses:

```text
cc_save_my_mod.dat
cc_save_my_mod.dat.bak
```

Only letters, numbers, `_`, and `-` are kept in the save name. Invalid or empty names fall back to `mod`. Do not rename or reuse a save name after releasing a mod unless you intentionally want a separate save file.

### Character Assets

HookCrashers checks the character folder for portrait assets:

```text
mods/example_mod/characters/troll/
  portrait.swf
  portrait_fresh.swf
```

If only `portrait.swf` exists, it is reused as the fresh portrait. `portrait_fresh.swf` is optional.

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
2. HookCrashers finds that character's `portrait.swf`.
3. When `lobby.cok6` loads, HookCrashers extracts the first `DefineShape` from `portrait.swf`.
4. The imported shape is assigned a new character id after the vanilla lobby shapes.
5. HookCrashers patches the lobby portrait sprite by cloning the vanilla frame that places the last vanilla portrait shape (`153`) at depth `1`.
6. The cloned frame keeps the vanilla transform/alignment and swaps only the placed character id to the imported portrait shape.

This means the mod portrait should inherit the same position, scale, and depth behavior as the vanilla portrait frame. The injected shape is appended at runtime; the original `.pak` stays untouched.

HookCrashers writes a debug dump of the patched lobby SWF to:

```text
CastleCrashers/HookCrashersDumps/lobby_patched.swf
```

### portrait.swf Requirements

`portrait.swf` should be a tiny uncompressed SWF containing one portrait shape.

Requirements:

- Use `FWS`, not `CWS` or `ZWS`.
- Include exactly one intended `DefineShape`/`DefineShape2`/`DefineShape3`/`DefineShape4` portrait shape.
- Put the portrait art in the same coordinate space and visual scale as the vanilla lobby portraits.
- Prefer `DefineShape3` when alpha/RGBA data is needed.
- Do not rely on frame scripts, sprites, buttons, or timeline behavior inside `portrait.swf`; HookCrashers imports the shape definition, not the whole movie.
- Keep one portrait per character folder. Shared portrait files are allowed, but shared paths will reuse the same injected definition.

For in-game sprites and animation assets, keep using `main.swf`/game asset authoring for now. The runtime portrait injector only handles lobby portrait shapes.

## Localization

`locs.json` declares localization strings used by the mod.

```json
{
  "strings": [
    {
      "unlocalized_name": "example_mod_character_name",
      "english": "Example Mod Character",
      "italian": "Personaggio Mod Esempio"
    }
  ]
}
```

HookCrashers reads the `strings` array and tracks each `unlocalized_name` or `id`. Custom localization ids start at the fixed base id `5000`.

## Multiplayer Notes

Expanded multiplayer character sync currently keeps the original packet replacement disabled. The stable path is to let the game use its original sync packet while HookCrashers patches the roster split/loop limits and rebuilt lobby character tables.

Practical rules for testing:

- Every modded player should use the same HookCrashers build.
- Every modded player should use the same folder mods.
- Character registration order must match.
- Vanilla clients are not protected yet. A vanilla client can still join and may desync or crash when addon slots are involved.

A future mod manifest/lobby compatibility layer should make modded lobbies visible only to compatible clients or provide a clear modded/vanilla choice before joining.

## C++ ASI API

Advanced binary mods can include `HookCrashers.h` and call the exported API.

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

Custom SWF native registration:

```cpp
HookCrashers::RegisterCustomSWF(
    0x150,
    "MyNativeFunction",
    [](int argCount, HC_SWFArgument** args, HC_SWFReturn* ret) {
        HookCrashers::SWF::ArgsReader reader(argCount, args);
        HookCrashers::SWF::ReturnValue out(ret);
        out.SetInt(reader.GetInt(0, 0) + 1);
    });
```

Overrides can be registered with `HookCrashers::RegisterOverride(...)`, and the original game handler can be reached with `HookCrashers::CallOriginal(...)`.

Useful helpers exposed by the API include:

- logging: `LogInfo`, `LogWarn`, `LogError`
- SWF strings: `AddString`, `GetString`
- player helpers: `GetPlayerObject`, `GetPlayerSelectedCharacterType`, `IsOnlineMode`
- save helpers: `FindCastleCrashersSavePath`, `GetCapturedSaveData`

## Troubleshooting

- Check `HookCrashers.log` first. Lua syntax errors, registration errors, localization loads, addon character loads, network patch setup, and SWF patch failures are written there.
- If a mod folder is ignored, make sure it contains `main.lua`, `manifest.json`, `locs.json`, or `icon.png`.
- If the save screen behaves like vanilla, check that `main_patched.swf` is being dumped and that the log reports the `f_InitSaveSystem` runtime patch.
- If a portrait is missing, check that `portrait.swf` is uncompressed `FWS` and contains a real `DefineShape` tag.
- If a portrait appears in the wrong place, inspect `lobby_patched.swf` and confirm the injected frame was cloned from the frame placing shape `153`, not an unrelated frame using another shape.
- If multiplayer desyncs, first test with exactly one addon character and identical mod folders on both clients.

## Disclaimer

HookCrashers is intended for modding, research, and creative extension of Castle Crashers Remastered.

Do not use HookCrashers to build cheats, trainers, piracy tools, DLC unlockers, or anything designed to give an unfair multiplayer advantage. Mod responsibly and keep backups of original game files and saves.