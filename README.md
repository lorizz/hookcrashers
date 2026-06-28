# HookCrashers

HookCrashers is an ASI modding platform for **Castle Crashers Remastered**.

This release focuses on the stable parts of the project:

- loading Lua folder mods from `mods/<mod>/`
- registering addon characters
- expanding the save/character tables for registered addon characters
- loading custom localization metadata
- exposing a C++ API for advanced ASI mods and custom SWF native calls

Runtime SWF editing is intentionally disabled in this release. HookCrashers does
not inject portrait art, shapes, sprites, or ActionScript into SWF buffers at
load time. Any visual/UI changes must be made manually in the game assets and
repacked into the relevant `.pak` files.

## Installation

1. Copy `HookCrashers.asi` and its required runtime files into the Castle
   Crashers Remastered game folder.
2. Start the game once. HookCrashers will create `HookCrashers.ini` and `mods`
   if they do not already exist.
3. Place folder mods under:

```text
CastleCrashers/
  HookCrashers.asi
  HookCrashers.ini
  mods/
    my_mod/
      manifest.json
      main.lua
      locs.json
      characters/
        my_character/
          portrait.svg
```

Back up your save data and original `.pak` files before testing character or SWF
changes. Changing addon character order after saves exist can remap addon slots.

## Configuration

`HookCrashers.ini` is created in the game folder.

```ini
[HookCrashers]
ShowExternalConsole=false
EnableOverlay=true
OverlayToggleKey=Home
```

Save expansion is always driven by Lua registrations. Custom localization is
always enabled for `mods/<mod>/locs.json`, and custom localization ids start at
the fixed base id `5000`.

## Folder Mods

Each folder inside `mods` is scanned independently. A folder is considered a mod
when it contains at least one supported file: `manifest.json`, `main.lua`,
`locs.json`, or `icon.png`.

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
    "my_character",
    HC.Enums.Weapon.Pitchfork,
    HC.Enums.Pet.None,
    true,
    false
)
```

The same function is also exposed as `HC.RegisterCharacter(...)`.

#### Save file name

HookCrashers redirects Castle Crashers saves away from the vanilla
`cc_save.dat`. By default it uses:

```text
cc_save_mod.dat
cc_save_mod.dat.bak
```

Use `RegisterSaveName` before registering characters when a mod needs its own
save file:

```lua
local HC = require("HookCrashers")

HC.RegisterSaveName("my_mod")
-- or: HC.Save.RegisterName("my_mod")

HC.Character.Register(
    "my_character",
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

Only letters, numbers, `_`, and `-` are kept in the save name. Invalid or empty
names fall back to `mod`. Do not rename or reuse a save name after releasing a
mod unless you intentionally want a separate save file.

`HC.Character.Register(id, weapon, pet, initiallyUnlocked, freshOnly)`:

- `id`: stable internal character id. Use lowercase ASCII names without spaces.
- `weapon`: starting weapon id. Prefer `HC.Enums.Weapon.*`.
- `pet`: starting animal orb id. Prefer `HC.Enums.Pet.*`.
- `initiallyUnlocked`: `true` if the character should start unlocked.
- `freshOnly`: `true` if this character is intended only for the fresh/custom
  character path. Use `false` when the UI should expose it in both classic and
  fresh/custom contexts.

See [docs/enums.md](docs/enums.md) for the complete weapon and pet enum list.

Registration order matters. The order in Lua is the order HookCrashers uses for
addon character slots, and it must match the order used in your manual SWF UI
edits.

The loader also checks the character folder for optional portrait metadata:

```text
mods/my_mod/characters/my_character/
  portrait.svg
  portrait_fresh.svg
```

`portrait.png` and `portrait_fresh.png` are also detected. If only `portrait.*`
exists, it is reused as the fresh portrait path. In this release these files are
not injected into SWFs automatically; they are useful as source assets for your
manual SWF/.pak pipeline.

### locs.json

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

HookCrashers reads the `strings` array and tracks each `unlocalized_name` or
`id`. Custom localization ids start at the fixed base id `5000`.

## Manual SWF and .pak Work Required

HookCrashers handles the runtime character/save logic, but the original SWF UI
still needs to know how to draw and select the new character. For this release,
you must patch the SWF files manually and repack them into the game `.pak`
assets.

The required manual work is:

1. Extract the `.pak` that contains `main.swf` and any related menu/lobby SWFs
   used by your build.
2. Open `main.swf` in a SWF editor/decompiler.
3. Add the new character portrait art as a real SWF symbol/shape, matching the
   same bounds, transform, and depth behavior as the existing portrait symbols.
4. Clone the existing portrait frame/symbol entry that matches the slot type you
   are adding.
5. Replace only the portrait character id in the cloned `PlaceObject2`.
6. Update any ActionScript/UI arrays, counts, or selection logic so the UI
   includes the addon character.
7. Repack the edited SWF into the correct `.pak`.

Current known lobby/select layout notes from the Steam build:

- the lobby portrait container is `DefineSprite 179`
- existing portrait frames around `133` and `135` are useful templates
- frame `133` places an existing portrait shape at depth `1`
- frame `135` places another existing portrait shape at depth `1`
- a cloned portrait frame should keep the original matrix/position/scale
- the `PlaceObject2` for the injected portrait must use:
  - depth `1`
  - `HasCharacter=true`
  - the new portrait shape character id
  - the same transform as the copied frame

If `freshOnly` is `true`, add the portrait only to the fresh/custom UI path. If
`freshOnly` is `false`, add it to both the classic and fresh/custom UI paths
used by the SWF. The exact frame numbers can differ between game builds or
patched assets, so use the nearby vanilla portrait frames as the source of truth
instead of hardcoding offsets blindly.

If this SWF/.pak step is skipped, HookCrashers can still register the character
for save/runtime logic, but the game UI may show missing portraits, wrong
selection frames, or no visible entry.

## C++ ASI API

Advanced binary mods can include `HookCrashers.h` and call the exported API.

Character registration:

```cpp
#include "HookCrashers.h"

HookCrashers::RegisterCharacter(
    "my_character",
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

Overrides can be registered with `HookCrashers::RegisterOverride(...)`, and the
original game handler can be reached with `HookCrashers::CallOriginal(...)`.

Useful helpers exposed by the API include:

- logging: `LogInfo`, `LogWarn`, `LogError`
- SWF strings: `AddString`, `GetString`
- player helpers: `GetPlayerObject`, `GetPlayerSelectedCharacterType`,
  `IsOnlineMode`
- save helpers: `FindCastleCrashersSavePath`, `GetCapturedSaveData`

## Runtime SWF Injection Status

The experimental lobby portrait runtime injector has been detached for this
release. `SWFScene_ParseHeader` now immediately calls the original parser and no
longer patches the SWF buffer before the game consumes it.

Source files for previous SWF experiments may still exist in the repository for
future work, but they are not part of the stable release flow.

## Troubleshooting

- Check `HookCrashers.log` first. Lua syntax errors and registration errors are
  written there.
- If a mod folder is ignored, make sure it contains `main.lua`, `manifest.json`,
  `locs.json`, or `icon.png`.
- If a character registers but is not visible in the UI, your manual SWF/.pak
  edit is missing or the addon order does not match the Lua registration order.
- If a portrait appears in the wrong slot, verify the SWF frame/order and the
  order of `HC.Character.Register(...)` calls.
- For multiplayer testing, every player should use the same HookCrashers build,
  same folder mods, same registration order, and compatible patched `.pak`
  assets.

## Disclaimer

HookCrashers is intended for modding, research, and creative extension of Castle
Crashers Remastered.

Do not use HookCrashers to build cheats, trainers, piracy tools, DLC unlockers,
or anything designed to give an unfair multiplayer advantage. Mod responsibly and
keep backups of original game files and saves.
