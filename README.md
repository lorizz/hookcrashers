# HookCrashers

HookCrashers is an open-source ASI modding platform for **Castle Crashers Remastered**.

Mods are folders: they register addon characters, add translations, portraits
and in-game menus, and HookCrashers applies everything at runtime. Nobody has to
edit or repack the game's `.pak` files.

- Lua folder mods (`mods/<mod>/`) and native Win32 ASI mods (`mods/<mod>/*.asi`)
- Addon characters: save layout expansion, roster and multiplayer sync, lobby portraits
- Runtime `main.cok6` save-system patch and `lobby.cok6` portrait injection
- Custom localization in all eleven game languages
- [Menu API](docs/menu-api.md) for native in-game menus (experimental)
- In-game prompts, a character data API and update callbacks for native mods
- Four texture quality presets (Low, Medium, High, Ultra)
- A larger game memory pool (1 GiB by default) for Ultra graphics
- Verified in-game updates (stable and testing channels)

## Requirements

- Castle Crashers Remastered on Steam, current build (`castle.exe` PE timestamp
  `0x6A4D42BF`, July 2026). HookCrashers checks the build before touching the
  game; on any other build it shows a message and changes nothing.
- Windows, or macOS/Linux through CrossOver or Wine.

## Install

1. Download **HookCrashers-v5-dropin.zip** from the
   [latest release](https://github.com/lorizz/hookcrashers/releases/latest).
2. Extract it into the Castle Crashers folder, the one that contains `castle.exe`
   (Steam → Castle Crashers → Manage → Browse local files).
3. Start the game once. HookCrashers creates `HookCrashers.yaml`,
   `HookCrashers.log` and an empty `mods` folder.

The zip contains `HookCrashers.asi`, `dinput8.dll` (Ultimate ASI Loader, which
loads every `.asi` in the game folder) and the `mods` folder.

On CrossOver, `scripts/install-crossover.sh` downloads the latest release into
the default bottle and enables the native `dinput8` override
(`HOOKCRASHERS_TAG=dev` installs the development build instead).

To uninstall, delete `HookCrashers.asi` and `dinput8.dll` (and `mods`, if you no
longer need your mods). Your vanilla save is never modified; modded saves are
separate files (see [Save files](#save-files)).

## Updates and release channels

HookCrashers checks for updates before the main menu opens and asks before
installing anything. Updates come only from the
[releases of this repository](https://github.com/lorizz/hookcrashers/releases):

| Channel | Who gets it | Published from |
|---|---|---|
| `stable` (default) | everyone | every push to `main` that bumps `VERSION` (`release_<version>`) |
| `testing` | testers with a testing key | every push to any other branch (`dev` prerelease, version `<VERSION>-dev`) |

Each release carries a channel manifest (`stable.json` or `testing.json`) with
the SHA-256 of `HookCrashers.asi`. The updater downloads only from
`github.com/lorizz/hookcrashers/releases/download/` and rejects any file whose
hash does not match. Every binary is built by the public GitHub Actions workflow
from a public commit, and each release lists the checksums of all its files.

Set `updates_enabled: false` in `HookCrashers.yaml` to disable update checks.

> Builds up to 5.1.2 read their updates from the retired
> `lorizz/hookcrashers-releases` repository. It is kept (archived) only to move
> them to 5.2.0, which reads from this repository.

## Configuration

`HookCrashers.yaml` in the game folder:

```yaml
updates_enabled: true
channel: stable
testing_key: ""
show_external_console: false
enable_overlay: true
overlay_toggle_key: Home
game_pool_mib: 1024
```

| Key | Meaning |
|---|---|
| `updates_enabled` | Check for HookCrashers updates at startup. |
| `channel` | `stable` or `testing`. Testing requires a valid `testing_key`; an invalid key resets the channel to stable. |
| `show_external_console` | Open a separate Windows console with the log. |
| `enable_overlay` / `overlay_toggle_key` | In-game log overlay (closed at startup); the key toggles it. |
| `game_pool_mib` | Game memory budget in MiB: `0` disables the expansion, otherwise `576`–`1536`. The vanilla pool is 576 MiB; HookCrashers grows it on demand. |

### Texture quality

Video Settings offers **Low, Medium, High, Ultra**. High restores the per-axis
texture sizing used before Painter Boss Paradise, Medium keeps the current
sizing and Ultra keeps its rendering settings (stored as value 3; old Ultra
configurations are migrated automatically). On 32-bit Windows, High is the
maximum. These presets are unrelated to the Classic/Fresh multiplayer mode.

## Folder mods

Each folder inside `mods` is a mod with a `manifest.yaml`:

```text
CastleCrashers/
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

Every file in `entrypoints` runs as Lua 5.4 when the game starts. Example mods
are in [`examples/mods`](examples/mods).

### Characters

```lua
RegisterCharacter("troll", Enums.Weapon.Pitchfork, Enums.Pet.None, true, false)
```

`RegisterCharacter(id, weapon, pet, initiallyUnlocked, freshOnly)`:

- `id`: stable internal id, lowercase ASCII without spaces.
- `weapon`, `pet`: starting weapon and animal orb (`HC.Enums.Weapon.*`,
  `HC.Enums.Pet.*`; full list in [docs/enums.md](docs/enums.md)).
- `initiallyUnlocked`: the character starts unlocked.
- `freshOnly`: only for the Fresh/custom character path; use `false` to show it
  in both Classic and Fresh.

Registration order matters: addon ids follow it, and it is part of the save
layout. Changing the order after a save exists remaps addon slots, and every
player in a multiplayer lobby needs the same mods in the same order.

### Save files

Whenever a folder mod is installed, HookCrashers uses a separate save named after
the first loaded mod's manifest `name` (`HookCrashers` removed, lowercase, only
letters, digits, `_` and `-`). `name: HookCrashers Example Mod` uses:

```text
hc_examplemod.dat
hc_examplemod.dat.bak
hcpbp_examplemod.dat   (Painter Boss Paradise settings)
```

The vanilla `cc_save.dat` is never used while a mod is loaded. Back up your
saves before testing character mods.

### Portraits

```text
mods/example_mod/characters/troll/
  portrait.swf          Fresh portrait
  portrait_classic.swf  Classic portrait
```

When `lobby.cok6` loads, HookCrashers imports the first `DefineShape` of each
file with a fresh character id. Classic portraits replace the free frames after
frame 34; Fresh portraits are inserted after frame 135. Cloned frames keep the
vanilla transform, so the portrait inherits the stock position and scale. A
missing file only skips that portrait.

Portrait SWFs must be uncompressed (`FWS`), contain the portrait as one
`DefineShape`–`DefineShape4` (prefer `DefineShape3` for alpha) and use the
vanilla portrait coordinate space. Sprites, buttons and scripts inside them are
ignored.

### Localization

`localizations.json` maps ids to text for `en`, `de`, `fr`, `it`, `ko`, `es`,
`zh_cn`, `zh_tw`, `ru`, `ja` and `pt`; missing translations fall back to English.

```json
{
  "en": { "characters.troll.name": "Troll", "characters.troll.unlock": "Unlocked by Example Character Pack." },
  "it": { "characters.troll.name": "Troll", "characters.troll.unlock": "Sbloccato da Example Character Pack." }
}
```

Characters use `characters.<id>.name` and `characters.<id>.unlock`. From
ActionScript, `GetLocalization("id")` returns the numeric id (from `5000` up) to
assign to `ntext`:

```actionscript
nameText.ntext = GetLocalization("characters.troll.name");
```

### Prompts and menus

```lua
HC.CreatePrompt("Enable hardcore mode?", function() print("yes") end, function() print("no") end)

local menu = HC.Menu.Create("mymod.settings", "My Mod")
menu:AddToggle("hardcore", "Hardcore mode", false, function(enabled) end)
menu:AddChoice("speed", "Speed", { "Slow", "Normal", "Fast" }, 2, function(index, text) end)
menu:Open()
```

The [Menu API](docs/menu-api.md) builds native menus (buttons, toggles,
choices, submenus, labels and graphics from your own SWF) with the game's own
navigation, controller input and sounds. It is experimental. See
[`examples/mods/MenuExample`](examples/mods/MenuExample).

## How the runtime patches work

- **Save system:** when `main.cok6` loads, HookCrashers rewrites the
  `f_InitSaveSystem` bytecode so it reads the active layout (character, addon,
  item, animal, level and relic ranges) from HookCrashers. Do not edit
  `f_InitSaveSystem` in `main.swf` yourself.
- **Executable patches:** every patch to `castle.exe` is checked against the
  expected original bytes first; if anything differs, nothing is written.
- **Debug dumps:** the patched movies are written to
  `HookCrashersDumps/main_patched.swf` and `lobby_patched.swf` for inspection;
  the game uses the patched buffers directly.

## Native (C++) mods

Each release ships **HookCrashers-SDK-Win32.zip** with `HookCrashers.h`, the
Win32 import library, examples and the [ASI tutorial](sdk/QUICKSTART-ASI.md).
Native mods are folder mods too:

```text
CastleCrashers/mods/MyNativeMod/
  manifest.yaml
  MyNativeMod.asi
```

```cpp
#include <HookCrashers.h>

HOOKCRASHERS_MOD_EXPORT bool __stdcall HookCrashersModInit() {
    HookCrashers::RegisterCharacter("troll", HookCrashers::Enums::Weapon::Pitchfork,
                                    HookCrashers::Enums::Pet::None, true, false);
    return true;
}

HOOKCRASHERS_MOD_EXPORT void __stdcall HookCrashersModShutdown() {}
```

Build a **Win32/x86** DLL with the `.asi` extension and link `HookCrashers.lib`.
Every `.asi` in the mod folder is loaded in filename order. Register characters
in `DllMain` or `HookCrashersModInit` so their `characters/<id>/` assets are
found. The C ABI covers version and logging, characters, prompts, menus and the
[current character data API](docs/current-character-api.md).

## Multiplayer

With addon characters, HookCrashers uses its expanded character sync (protocol
6): joining players adopt the host's Classic/Fresh mode, and every player needs
a HookCrashers build with protocol 6 (5.1.1 or later), the same mods and the same
registration order. Older protocol packets and malformed packets are rejected.
Without addons, the game's own sync is used.

Play Vanilla opens the native multiplayer flow with your mods loaded. The modded
lobby browser (Play Modded) is not available yet. Unmodified players joining an
addon lobby are not supported.

## Troubleshooting

Start with `HookCrashers.log` in the game folder: mod loading, Lua errors,
patches and update checks are all logged there.

- **"build not supported" at startup:** Steam updated the game; wait for a
  HookCrashers update. Nothing was changed.
- **Mod ignored:** check `manifest.yaml` and that every entrypoint exists.
- **Native mod ignored:** it must be `mods/<mod>/<file>.asi`, built for
  Win32/x86; the log shows the Windows loader error.
- **Save screen looks vanilla:** check that `main_patched.swf` is dumped and the
  log reports the `f_InitSaveSystem` patch.
- **Portrait missing or misplaced:** check the file name and format above, then
  inspect `lobby_patched.swf`.
- **Multiplayer desync:** test with one addon character and identical mod
  folders on every client.

## Building from source

Requirements: Visual Studio 2022 (MSVC v143, Win32 toolset).

```bat
git clone https://github.com/lorizz/hookcrashers.git
cd hookcrashers
git clone --depth 1 https://github.com/microsoft/Detours.git External\detours
git clone --depth 1 https://github.com/ocornut/imgui.git External\imgui
git clone --depth 1 --branch v5.4.7 https://github.com/lua/lua.git ..\External\lua54\lua-5.4.7\src
```

Build Detours for x86 (`nmake` in `External\detours` from a "x86 Native Tools"
prompt), then build `HookCrashers.vcxproj` as **Release | Win32**. The CI
workflow (`.github/workflows/build.yml`) runs the same steps. Offline tests are
in [`tests`](tests).

### Releasing (maintainers)

1. Bump `VERSION` and add a `## <version> - <date>` section to `CHANGELOG.md`.
2. Merge into `main`. The workflow publishes `release_<version>` with the
   drop-in zip, the SDK, checksums and `stable.json`. A push to `main` without a
   new version publishes nothing.
3. Any other branch publishes the `dev` prerelease for the testing channel.
   Commits containing `[skip ci]` do not run the workflow.

## Documentation

- [Menu API](docs/menu-api.md), [current character data API](docs/current-character-api.md),
  [prompts](docs/prompts.md), [enums](docs/enums.md)
- Technical notes: [graphics quality](docs/graphics-quality-5.1.2.md),
  [online menu](docs/online-menu-5.1.1.md), lobby crash investigations
  ([5.0.67](docs/lobby-crash-5.0.67-dev.md), [5.1.1](docs/lobby-setmember-crash-511.md))
- [Changelog](CHANGELOG.md)

## Disclaimer

HookCrashers is for modding, research and creative extension of Castle Crashers
Remastered. Do not use it to build cheats, trainers, piracy tools, DLC unlockers
or anything that gives an unfair multiplayer advantage. Keep backups of your
game files and saves.

## License

HookCrashers is free software under the [GNU General Public License v3.0](LICENSE).
You can study, modify and redistribute it; modified or redistributed versions
must stay under the GPL, publish their source code and keep the copyright
notices.

Copyright (C) 2024-2026 Alessandro Burza (ilVonBurza / lorizz).
