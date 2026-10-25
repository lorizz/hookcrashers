# Changelog

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

## 5.0.4 - 2026-09-27

### Fixed

- Added Classic-mode addon character selection for players who do not own or have disabled the Painter Boss Paradise DLC.
- Kept the proven 5.0.3 Fresh and Workshop character paths unchanged while applying the no-DLC fallback only to addon character unlock queries.
- Restored configured `initiallyUnlocked` addon slots and cleared stale Workshop unlock bits without overwriting existing addon progress.
- Made the ImGui mouse cursor appear while the debug console is open and disappear when the console closes.

### Changed

- Expanded the runtime Classic lobby selector scan to include every registered addon slot, with locked characters still filtered by their save data.

## 5.0.3 - 2026-09-26

### Added

- Native Win32/x86 ASI loading from dedicated `mods/<mod>/*.asi` folders, including deterministic load order and optional `HookCrashersModInit` / `HookCrashersModShutdown` exports.
- A packaged C++ SDK containing `HookCrashers.h` and `HookCrashers.lib`.
- Public native-mod logging and runtime version exports.

### Fixed

- Replaced stale C++ API documentation that referenced exports and headers absent from the v5 build.

## 5.0.2 - 2026-09-26

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
