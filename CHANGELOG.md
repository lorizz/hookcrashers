# Changelog

## Unreleased

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