# Changelog

All notable changes to HookCrashers. Stable releases are listed as
`## <version> - <date>`. `-dev` entries were development builds published to
the testing channel. The workflow uses the section matching `VERSION` as the
release notes.

## 5.3.0 - 2026-11-27

### Added

- **Save slots:** up to 10 modded saves (`hc_slot1.dat` … `hc_slot10.dat`), each with a metadata file (`hc_slotN.meta`), all synced by Steam Cloud.
  - A slot screen opens before the main menu on every launch with mods. You pick a slot with left/right, see when it was last played and with which mods, then Play/Continue, start a new game in an empty slot, open Mod Info, import the vanilla save or delete a slot.
  - Switching slots reloads the save without restarting the game. HookCrashers checks that the game really reloaded the slot, and cancels the switch otherwise, so one slot is never written over another.
  - **Mod Info** lists the mods a slot was last played with, colored: green (same version), orange (other version or missing, no effect on the save), red (save mod missing or changed: slot locked), blue (new since then).
  - The slot screen and Mod Info are translated into the 11 game languages.
  - **Import vanilla save:** copies `cc_save.dat` into an empty slot and adds the addon characters. `cc_save.dat` itself is only read, never written.
  - Deleting a slot moves its files to `hc_trash_slotN.*`; the slot in use cannot be deleted.
- **Save slots button in the main menu:** the row with the green Options (wrench) button and the red button now has three buttons; the new teal one on the right, with a save icon, opens the slot screen at any time. Left/right cycle through the three buttons, the mouse works too. The MENU movie is changed in memory when it loads (the game files are not modified), and only if it matches the supported build.
- **Locked slots:** mods that register characters change the save layout, so HookCrashers records them as save mods. A slot played with a save mod that is now missing or has another version shows as locked (red) and cannot be loaded; if it was the active slot, the game starts on another slot and the locked one is not loaded or changed. Mods that do not touch the save are only listed in Mod Info.
- **Character remapping:** each slot remembers which addon character is in which save position. When character mods are added or reordered, the save is rearranged by character name, so no character takes another's progress. The file before a rearrangement is kept as `hc_slotN.dat.old`.
### Fixed

- **Slot screen after deleting a slot:** the cursor stays on the deleted (now empty) slot, on the slot selector, instead of jumping to the active slot with the cursor on a greyed-out Delete that blocked the menu. Mod menus never keep the cursor on a disabled item.
- **Menu API input:** mod menus could be shown but ignored every key and controller button. They now start the game's fade-in (input is enabled when it ends) and read input from any controller when the menu they replace had no player assigned.

### Changed

- **Save file names:** saves no longer depend on the first mod's manifest name. On the first launch the save used until 5.2.0 (`hc_<manifest>.dat`) is copied into slot 1, and the original file is kept. The PBP config is now `hcpbp.dat` (copied from `hcpbp_<manifest>.dat` when present).

## 5.2.0 - 2026-11-24

### Added

- **Menu API (experimental):** mods build native in-game menus from Lua (`HC.Menu`) and C++ (`HookCrashers::Menu`). Menus can contain buttons, On/Off toggles, option choices, submenus, labels and graphics from the mod's own SWF. They use the game's own book background, navigation, controller input, sounds, fades and Back button. See `docs/menu-api.md` and `examples/mods/MenuExample`.
- **Menu graphics:** `HC.Menu.RegisterGraphic` imports an exported sprite of an uncompressed AS2 SWF into MENU with fresh character ids. Unsupported content (bitmaps, buttons, static text, video, PlaceObject3, compressed or AS3 SWFs) is rejected with a log message.
- **Release checksums:** each release lists the SHA-256 of every file and attaches `SHA256SUMS.txt`.

### Changed

- **Releases moved to `lorizz/hookcrashers`.** Builds and update manifests are published only in this repository, and `lorizz/hookcrashers-releases` is retired.
  - The updater reads `stable.json` from the latest release and `testing.json` from the `dev` prerelease.
  - It installs only files downloaded from this repository's releases.
  - Installs up to 5.1.2 are moved to 5.2.0 by a one-time migration release (`scripts/publish-legacy-bridge.sh`).
- **Release channels:** a push to `main` publishes the stable release when `VERSION` changes, and a push to any other branch publishes the `dev` prerelease (testing channel). Publishes on the same channel run one at a time.
- **Drop-in zip:** it no longer contains `mods/TestMod`. That mod enabled a test character and moved saves to `hc_testmod.dat` for everyone who installed HookCrashers. The example mods stay in `examples/mods`.
- **Menu table:** the main-menu handler table now has 64 entries (46 vanilla, 2 online menus, 16 mod menus). A single module patches it after checking the original bytes, for both the online menus and the Menu API.
- **`NativeDB/Natives.h`:**
  - removed 38 entries that were macOS offsets or mid-function addresses (calling them crashed the game);
  - corrected `CC_WriteSaveFile` (0xE9280), `CC_SyncCharacterListFromSave` (0x85B90), `CC_TextMenuItemCtor` (0x153EE0) and `CC_TextMenuItemSetLocalizedText` (0x154210).

  Every remaining entry is a verified function entry point.
- **Documentation:** `README.md` was rewritten (install, channels, configuration, building and releasing).

### Fixed

- **Lobby skin lookup corrupted with addons registered:** character expansion wrote `-networkBase` to 0x8D2D6, one byte too far. That byte is the opcode of the `cmp ecx, eax` that follows, which became `loopne` in the lobby skin lookup. It now patches the `lea ecx, [edi-0x20]` immediate at 0x8D2D5.
- **Double save hooks:** a second `CharacterExpansion::Install` no longer detours `CC_CreateDefaultSave` and `CC_SanitizeCharacterSaveSlots` again; before, the hooks ran twice. The two stats-iterator bytes that were written twice now have a single owner, and the result is unchanged.
- **`MenuStructs.h`:** the `TextMenuItem` owner and callback offsets were swapped. Owner is +0x34 and callback +0x38.
- **Release workflow:**
  - the manifest step hashed `../artifact/...` instead of `artifact/...`, so the 5.1.2 release had to be finished by hand;
  - `scripts/install-crossover.sh` looked for the removed `v5.0.0-draft` tag and for a `HookCrashers.yaml` that the zip does not contain. It now installs the latest release (or `HOOKCRASHERS_TAG=dev`).
- **Updater:** a missing manifest (HTTP error page) is no longer parsed as a manifest.
- **Menu graphics:** they are imported only into the stock MENU movie (the one exporting `playwithfriends`).

### Security

- **Character expansion:** the original bytes of all 79 constant and code patches are checked before any of them is written. If anything differs, nothing is written. Values from an earlier install are accepted, so a Lua `InstallCharacterExpansion` followed by the automatic install still works.
- **Save redirect:** the 11 `cc_save.dat` and `cc_save.dat.bak` references are checked to point at the vanilla strings before they are redirected. Before, only the PBP config references were checked.
- **Multiplayer sync:** incoming HC5 packets are checked against the received message length before the roster body is copied. A truncated or malformed packet from another player is rejected instead of being read past the end of the buffer.
- **Updater:** manifest downloads are limited to 64 KiB.

### Validation

- **Offline tests:** the menu model (66 checks) and SWF import (31 checks, including the original online selector and lobby sprites) pass under AddressSanitizer/UBSan.
- **Character expansion patches:** replayed against the 5.1.2 code with 0, 1 and 5 addons. All 77 constant addresses end with the same values, except the 0x8D2D5 fix.
- **Build:** the MSVC Win32 build passes in CI.
- **Not tested in game yet:** the Menu API, the packet bounds check and the new update source.

## 5.1.2 - 2026-11-23

### Open source and security

- HookCrashers is now open source under the GNU GPL v3.0. The full source code and history are public at `lorizz/hookcrashers`.
- The in-game updater installs only official builds from `lorizz/hookcrashers-releases`. It checks the SHA-256 published in the channel manifest and rejects any download whose hash or origin does not match.
- HookCrashers now checks the exact Castle Crashers build (PE timestamp `0x6A4D42BF`) before changing the game. If Steam updates the game, HookCrashers shows a "build not supported" message and leaves the game unchanged, so it cannot corrupt saves or crash on an unknown build.
- Release builds come from the public GitHub Actions workflow, and every published binary maps to a public commit.

### Fixed

- Restored the 5.0.0–5.0.4 character-patch activation behavior. Starting the online menus alone no longer replaces MAIN's `f_InitSaveSystem` or changes lobby character selectors. Registered character addons and explicit Lua/C++ character-expansion installs still enable the existing expansion patches. This preserves custom MAIN initialization in installations without character expansion.
- Crash reports now include the exact HookCrashers version and bounded register/stack memory samples. Stack labels derived only from the nearest export are marked as unverified so an unrelated API export is not mistaken for the crashing function.

### Added

- Four texture quality presets in order: Low (0), Medium (1), High (2), Ultra (3). High reuses the original localized label in all eleven languages.
- Restored High's historical per-axis texture sizing from the pre-Painter Steam build of 31 July 2024: optional power-of-two rounding followed by independent bounds, with the historical 512 limit and native minimum. Medium retains the current aspect-preserving sizing policy.

### Changed

- Moved Ultra's atlas selection, texture scaling, warning and defaults from value 2 to 3. Ultra's rendering parameters are retained. Ultra keeps the native WOW64 availability check; 32-bit Windows can select High as its maximum.
- Migrates existing `video/tex_quality=2` configurations to Ultra (3), with a persisted schema marker so a new High selection remains High after restart. This changes the graphics setting only, not character progress or saves.
- All graphics instruction sites and native calling conventions are validated before enabling the feature; incompatible code disables the entire feature.

### Validation

- Offline production-loader regression checks passed on the stock MAIN and the tester's modified MAIN: menu-only startup preserves each file byte-for-byte, explicit zero-addon expansion and registered-addon expansion retain initializer replacement, and activation/removal/failure/retry behavior is covered. Online-menu import is stubbed in these activation tests; the tester's in-game crash still needs a reproduction check.
- Release/Win32 build and 6,656 isolated native comparisons passed: historical High, relocated Ultra and unchanged Low/Medium. Production configuration/menu tests cover migration, four-preset round trips and native stack signatures; hook tests cover atomic signature rejection and full attach/patch/detach restoration.
- Historical sizing is restored; this does not revert the game's newer assets or the rest of its renderer. Full visual/gameplay verification remains necessary.

## 5.1.1 - 2026-10-27

### Multiplayer and menus

- Joining players now follow the host's graphics mode: a Fresh host switches clients to Fresh, and a Classic host switches clients to Classic. Expanded character synchronization preserves the game's host authority checks.
- Includes the addon unlock fix for players without the Painter Boss DLC and the corrected native roster insertion used during lobby synchronization.
- Restores the injected online selector and original custom SWF artwork. Play Vanilla opens the native multiplayer flow, including addon synchronization when mods are loaded. Play Modded is visibly disabled as `Play Modded - soon` while the custom lobby browser is unfinished.
- All modded players must use a compatible HC sync protocol (version 6), matching mods and character registration order. Older HC packets are rejected.

### Modding and performance

- Includes the 1 GiB effective game allocation budget which resolved the reported Ultra graphics loading crashes. The reported occasional Blacksmith level lag spike remains separate.
- Adds the C++ character snapshot API `GetCurrentCharacterData` for player slots 1..4 and game-thread Update callbacks for native ASI mods.
- Includes the Win32 SDK, import library, ASI mini tutorial and buildable CharacterLevelWatch example.

### Validation

- Promoted after positive tester feedback on 5.1.1-dev. Local Release/Win32 build, thirteen supported-executable checks and offline production sender/receiver tests passed, including all four Classic/Fresh host/client combinations.
- The character API's complete in-game player lifecycle still requires consumer validation. The custom lobby browser is not available in this release.

## 5.1.1-dev - 2026-10-26

### Added

- Restored the original injected multiplayer selector and custom SWF artwork: Play Vanilla follows the native Play With Friends flow; Play Modded opens the injected lobby screen. Assets are embedded in the ASI.

### Fixed

- Expanded character synchronization now transmits the actual Classic/Fresh setting instead of always advertising Fresh. Joining clients adopt the host's mode before the roster becomes ready, using the native authority checks and mode setter. Clients cannot change the host's mode.
- Corrected the mode setter address and its x86 stdcall calling convention, with executable signature checks before installation.
- Updated menu integration for the supported executable, including state allocation, sprite/font ID remapping, full physical MENU length and native menu layout calls.

### Compatibility and validation

- HC character-sync protocol is now version 6. All modded peers must update and use matching addon layouts; old or malformed HC packets are rejected. A valid Classic/Fresh difference is synchronized, not rejected.
- The restored custom lobby screen starts empty. The former demonstration lobby provider and real-lobby joining are not implemented by this restoration.
- Offline production sender/receiver tests cover all four host/client mode combinations, host authority, malformed metadata and native fallback. Two-account Steam gameplay still requires verification.

## 5.1.0-dev - 2026-10-26

### Added

- Expanded the C++ SDK with `GetCurrentCharacterData(slot)` for HUD/player slots 1..4: character identity, health/magic, level/XP, gold, stats, equipment, consumables and other runtime values. Numeric validity and explicit live/HUD/loading/stale states distinguish unavailable fields from zero.
- Added bounded enumeration of ActionScript properties, arrays and nested objects, including addon fields, with sequence checks and truncation/read-fault flags. Public snapshots contain no raw VM pointers.
- Added `RegisterUpdateCallback` / `UnregisterUpdateCallback`: a `void __stdcall` callback runs on the game thread after snapshot publication. Successful unregistration waits for active calls. Duplicate registrations are rejected and faulting callbacks are disabled.
- Added a C++ ASI mini tutorial and buildable CharacterLevelWatch example with `void Update()`, four-player level-increase polling and baseline resets for loading or identity changes.
- Committed the x86 import library under `sdk/lib` and added `HookCrashers-SDK-Win32.zip` to private/public release assets. CI verifies the packaged example links against the freshly built SDK.

### Changed

- Starts the 5.1 C++ runtime API series. Release versions now come from the explicit `VERSION` file rather than the CI run number; development builds append `-dev`.
- Retains the 1 GiB pool and multiplayer native calling-convention/roster fixes from 5.0.68-dev.

### Validation

- Release/Win32 build, ten supported-executable compatibility checks, production API tests on a four-player VM model, and SDK consumer/example compilation passed locally.
- Offline tests cover isolated actual native getters/hash/map/array routines, the ECX/XMM1 frame bridge, Detours attach/capture/detach on the native disabled-update branch, callback ordering, level-up comparison, fault isolation, concurrent reads and synchronized unregistration.
- No game was launched. The full in-game object chain/lifecycle and achievement behavior still require tester verification; polling does not recover intermediate changes between captures or loading gaps.

## 5.0.68-dev - 2026-10-25

### Fixed

- Corrected the no-Painter-DLC addon unlock fallback to call the game's native with `__stdcall` and an AL Boolean return. The previous `__cdecl` declaration removed its two arguments twice, unbalancing the stack by eight bytes during lobby script evaluation.
- Corrected the expanded character sync receiver's roster insertion target from RVA `0x12B0E0` (inside an unrelated instruction) to the native entry at `0x2B030`, as called by the vanilla receiver. This separate defect predates the no-DLC fallback.
- Added instruction checks for all unlock-native return paths and the roster insertion entry/return before enabling the affected hooks.

### Validation

- Local Release/Win32 build and eight executable compatibility checks passed.
- Offline tests using isolated native routines reproduced the old eight-byte stack imbalance, passed 100000 corrected unlock calls across four players, and verified existing/new roster ID insertion with an isolated insertion dependency.
- Retains the 1 GiB game pool from 5.0.67-dev. Tester feedback confirms Ultra graphics now loads; the Blacksmith level lag spike remains under investigation. Multiplayer lobby gameplay with these fixes still needs tester verification.

## 5.0.67-dev - 2026-10-25

### Fixed

- Raised the effective main game allocation budget from 576 MiB to 1024 MiB using on-demand overflow arenas, including native free routing and reset cleanup. Existing allocations retain their addresses. This targets the null-buffer allocation failure observed with Ultra graphics and during multiplayer loading.
- Corrected the addon unlock fallback native address and validated its instructions before enabling the fallback; added numeric argument conversion and player-index checks.
- Retained only the final successful patched SWF buffer instead of retaining intermediate copies, with synchronized ownership bookkeeping.

### Added

- Configurable `game_pool_mib` (default 1024; 0 disables expansion; supported budgets 576..1536 MiB), allocator compatibility checks, and growth/failure logging.
- Offline tests against allocator routines from a user-provided executable snapshot, including actual Detours routing, 16 MiB zero-fill, budget enforcement, free/reset, and overflow concurrency. No proprietary executable snapshot or extracted fixture is included.

### Validation

- Local Release/Win32 build, five executable compatibility checks, and native allocator/core hook integration tests passed. Ultra graphics and multiplayer gameplay have not yet been validated with this build.

## 5.0.66-dev - 2026-09-27

### Fixed

- Added Classic-mode addon character selection for players who do not own or have disabled the Painter Boss Paradise DLC.
- Kept the proven 5.0.3 Fresh and Workshop character paths unchanged while applying the no-DLC fallback only to addon character unlock queries.
- Restored configured `initiallyUnlocked` addon slots and cleared stale Workshop unlock bits without overwriting existing addon progress.
- Made the ImGui mouse cursor appear while the debug console is open and disappear when the console closes.

### Changed

- Expanded the runtime Classic lobby selector scan to include every registered addon slot, with locked characters still filtered by their save data.

## 5.0.65-dev - 2026-09-26

### Added

- Native Win32/x86 ASI loading from dedicated `mods/<mod>/*.asi` folders, including deterministic load order and optional `HookCrashersModInit` / `HookCrashersModShutdown` exports.
- A packaged C++ SDK containing `HookCrashers.h` and `HookCrashers.lib`.
- Public native-mod logging and runtime version exports.

### Fixed

- Replaced stale C++ API documentation that referenced exports and headers absent from the v5 build.

## 5.0.64-dev - 2026-09-26

### Fixed

- Isolated Painter Boss Paradise configuration per mod as `hcpbp_<modname>.dat`, preventing stale vanilla or another mod's Workshop selections from being loaded by HookCrashers.
- Made save and PBP filename redirection atomic and rejected unexpected PBP filename operands from unsupported executable revisions.

## 5.0.2.1-dev - 2026-08-03

### Added

- Expanded the runtime SWF string arena, pointer table, and hash table for code-heavy custom SWFs.

### Fixed

- Restored the vanilla menu return target after closing the updater prompt, so the Classic/Fresh selector only appears when requested by the game.
- Right-aligned custom version text against the game's original bottom-right anchor and adjusted multiline vertical placement.
- Added bounds checks before inserting custom localization strings into the SWF string table.

## 5.0.1 - 2026-08-02

### Added

- Custom mod localization loading from `localizations.json`, with English fallback and support for all eleven game languages.
- The ActionScript `GetLocalization("id")` native, returning a HookCrashers localization id for assignment to `ntext`.

### Fixed

- Updated the custom SWF registration, string cache, dispatcher, and `ntext` lookup RVAs for the current Castle Crashers executable.

## 5.0.0 - 2026-08-02

### Added

- Localized updater prompt text for all eleven game languages, selected from the runtime ProfileManager language index.
- Native book-style confirmation prompts exposed through `CreatePrompt` for Lua and C++.
- In-game HookCrashers update prompt backed by the configured stable or testing release channel.
- Runtime `main.cok6` save-system patching. HookCrashers now manages the save initialization bytecode internally; mod authors should not edit `f_InitSaveSystem()` in `main.swf`.
- Runtime `lobby.cok6` portrait injection from per-character `portrait.swf` files.
- `portrait_fresh.swf` support. When present, `portrait.swf` is used for classic mode and `portrait_fresh.swf` is used for fresh/custom mode.
- Classic portrait frame injection for non-`freshOnly` addon characters. Classic portraits are cloned from frame 34 of the lobby portrait sprite and inserted before the frame 35 depth-1 removal.
- Fresh portrait frame injection from the existing fresh portrait template frame.
- Runtime SWF dumps for inspection:
  - `HookCrashersDumps/main_patched.swf`
  - `HookCrashersDumps/lobby_patched.swf`
- Character-id allocation for injected SWF definitions. Imported portraits now use the next free character id after the existing SWF character list and are appended at the end of the root tag list.
- Expanded multiplayer roster split/loop patches for addon slots while keeping the original character sync packet path.
- Folder-mod documentation for runtime-only modding without repacking `.pak` files.

### Changed

- Update checks now run before the main menu and only open the update prompt when a newer release exists. Confirming downloads the ASI, replaces it after shutdown, and restarts the game.
- Lua mods now run on the embedded Lua 5.4 runtime.
- The intended workflow is now modular and folder-based: Lua registration, localization, save expansion, and lobby portraits are handled at runtime by HookCrashers.
- Runtime portrait injection now appends imported definitions immediately before the root `End` tag instead of inserting them in the middle of existing SWF character definitions.
- Lobby portrait frame cloning no longer uses broad fallback sprite detection; it targets the configured lobby portrait sprite only.
- `README.md` now documents the runtime save patch, runtime portrait injection, `portrait.swf` requirements, and multiplayer compatibility notes.

### Fixed

- Fixed malformed `ActionDefineFunction` patching for the obfuscated `f_InitSaveSystem` bytecode layout used by `main.swf`.
- Fixed addon multiplayer visibility/desync caused by missing split/loop roster patches.
- Fixed portrait injection collisions with existing SWF character ids.
- Fixed imported portraits being inserted between existing shape definitions instead of at the end of the SWF root tag list.
- Fixed fresh-only behavior so classic portraits are only generated when the Lua character registration has `freshOnly = false`.

### Known Notes

- All modded multiplayer clients must currently use the same HookCrashers build, same mod folders, and same character registration order.
- Vanilla clients are not filtered yet and may desync or crash when joining modded addon-character lobbies.
- `portrait.swf` should be uncompressed `FWS` and contain the intended portrait shape. HookCrashers imports the shape definition, not the whole SWF timeline.
