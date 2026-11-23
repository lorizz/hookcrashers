# Single-player lobby execute-at-1 crash (5.1.1)

Status: crash site identified and a separate, concrete incompatibility with the supplied MAIN.swf established. The link between that incompatibility and the invalid virtual call remains **unproven**. Dedicated-server work is paused. No game was launched for this investigation.

## Verified evidence

The tester reports a failure when entering the single-player character-selection lobby, at the end of loading, with HookCrashers 5.1.1. The user initially described this as vanilla, then corrected that description: at least MAIN.swf is modified and supplied that file. The set of other installed assets is unknown. The exception is an execute access violation at address `0x00000001`.

The official `release_5.1.1` ASI was downloaded and examined independently of locally compiled candidates:

* SHA-256: `6d519b8496efddfbbb3b19723a0967547cf4f3b951cdf717bc139167c77fa83a`.
* `HookCrashers_GetCurrentCharacterProperty` export RVA: `0x801E0`.
* Reported ASI frame: `0x96A03`; displacement from that export is `0x16823` (92195), exactly matching the report.
* The instruction before `0x96A03` calls the original SWF dispatcher. Arguments and surrounding branches identify the production `Localization::HookedDispatcher` fallback. It is **not an invocation of GetCurrentCharacterProperty**. DbgHelp's nearest-export name must not be treated as the function identity.

The supported runtime snapshot in Ghidra has image base `0x009E0000`, SHA-256 `d3fd45ceec0dfa227ca2d41a7e22a280fa8ffb02d0658ea33125cd1d4ee9d793`. Addresses below are RVAs so ASLR does not affect the comparison. The tester executable's exact hash is unavailable.

* Opcode table index `0x4F` (AVM1 SetMember), at snapshot VA `0x00BA5A9C`, points to RVA `0x113190`.
* Handler renamed `CC_AVM1_ActionSetMember` in Ghidra.
* At RVA `0x11331E`, the handler executes `call dword ptr [edx+0x10]`; its return address is `0x113321`, exactly the first game frame in the report.
* Here `ECX` is the destination object, `EDX` its virtual-method table, `EAX` the converted member-name ID, and `EBX` the value operand slot.
* Tester registers: receiver `0x087C3180`, table `0x07F01CC4`, name ID `0x72`, operand slots ESI `0x0807A0D8`, EDI `0x0807A0E8`, EBX `0x0807A0F8`.
* This is consistent with an invalid virtual method target of `1` while assigning an ActionScript property. The missing object/table bytes prevent confirming the precise memory state or its origin. Member ID `0x72` is runtime-specific and cannot reliably be translated using another run's string table.

## What this does not establish

The crash report alone does not establish file-size exhaustion, a missing addon character, a save-system conflict, an API defect, or which component originally invalidated the object. The supplied SWF establishes the save-initializer incompatibility described below, but not that it caused this exact invalid call. Use-after-free, overwritten object storage and an incorrectly typed operand remain hypotheses. Spreading asset loading across frames or enlarging another pool is not a demonstrated fix.

The current-character public property function copies a cached snapshot. Its absence from this active call path does not exclude an earlier side effect elsewhere in HookCrashers. Likewise, repeated dispatcher frames represent nested SWF calls and do not by themselves prove runaway recursion.

The report was written by a first-chance vectored exception handler; it should be correlated with the actual process exit because a first-chance exception can also be handled later.

## Supplied MAIN.swf: established compatibility defect

File examined: `MAIN.swf` (extracted locally), 1,477,708 bytes, uncompressed FWS version 6, SHA-256 `bb4ee37684303db692748f3298429980669e66fecc40c60c4dd73487787748c5`.

The file contains Refracted identifiers and substantial custom game logic, including loadouts, extra animals and custom character/magic handling. This is not an otherwise stock MAIN with only cosmetic edits. The identifiers establish contents, not the exact mod release or the set of accompanying files installed by the tester.

`src/SWF/Cok6LoadHook.cpp` replaces the entire `f_InitSaveSystem` definition with `BuildInitSaveSystemBytecode()`. `TryPatchMainSaveSystem` runs on MAIN paths without requiring any registered addon characters. Both this loader and the builder/registry are unchanged between official 5.1.1 commit `be8a71f08e4b3244f35b5ea8c243b210ef507219` and the current diagnostic base.

The supplied function is in root frame 2 at byte offset 239102 (body begins at 239126, 347 bytes). A Python reproduction of the production action-walker rules reaches this definition before its first ActionEnd. This reachability check is a source-model check, not an in-game observation of the tester's process.

With zero characters registered through HookCrashers, the replacement changes the following values:

| Field | Supplied initializer | HookCrashers initializer |
| --- | ---: | ---: |
| `num_animals` | 40 | 32 |
| `num_special_animals` | 4 | absent |
| `num_characters_legacy` | 31 | 32 |
| `num_characters` | 77 | 72 |
| `num_base_characters` | 72 | absent |
| `num_loadouts` | 8 | absent |
| `loadout_size` | 24 | absent |

These fields are absent because the replacement creates a new `save_data_info` object. They cannot survive from an earlier initializer.

Concrete consumers in the supplied ActionScript:

* `f_FirstTimeInitOnce` calls `f_InitSaveSystem` before initializing players and requesting the lobby.
* `f_LoadCharacterData` sizes `animal_unlocks` using both `num_animals` and `num_special_animals`, and calls `f_GetCharacterLoadouts` and `f_GetEndlessScores` before marking the HUD loaded.
* `f_GetCharacterLoadouts` creates `loadout1` through `loadout8`, each containing 24 storage bytes. With `num_loadouts` absent, its loop does not initialize these properties under standard AVM1 undefined-number comparison semantics.
* `f_SafePlayerType` uses `num_base_characters` to distinguish custom characters from workshop aliases. In the supplied function, character 73 maps to save slot 43 (`73 - 3*10`). With that field absent, the alternate calculation maps it to 33 (`32 + ((73-1-32)%10 + 1)`). This is an arithmetic consequence of the supplied code, not a claim that the tester selected character 73.
* `f_IsCharacterUnlocked`, `f_BlacklistedCharacterSwitch` and `f_SetPlayerMagic` also use the missing base-character boundary.
* Load/save routines compute extra-storage offsets from `num_loadouts * loadout_size`; this layout must be preserved, not replaced by arbitrary defaults.

This proves a data-contract conflict even without a HookCrashers addon package. It does **not** prove that an undefined field alone can turn a valid AVM1 object into a virtual table with target 1. The exact crashing assignment and the earlier invalidation still require runtime operands/object memory or a reproducible trace.

### Regression boundary: tester recalls 5.0.4 working

The user subsequently reported that the same tester recalls an older HookCrashers, possibly 5.0.4, working. The exact previous binary and identical installed assets have not been confirmed, so this is a useful regression constraint rather than a controlled A/B reproduction.

Repository history establishes a specific change consistent with that observation:

* 5.0.4 is commit `ede8377` (release commit; there is no local 5.0.4 tag). Its bootstrap installs `CharacterExpansion` only when `Registry::AddonCount() > 0`. The COK6 loader hook is installed from `CharacterExpansion::Install`. Without registered addon characters, the MAIN initializer replacement is therefore inactive in that bootstrap path.
* Commit `e81f3d5` (5.1.1-dev online-menu restoration) adds an unconditional `Cok6LoadHook::Install()` to bootstrap, before addon loading, to enable menu asset injection. Official 5.1.1 includes this change.
* The initializer builder and registry code themselves did not change between 5.0.4 and 5.1.1. Their destructive replacement became newly reachable with zero addon characters because the loader is now always installed. Its save-initializer branch has no independent addon-count guard.

Consequently, a zero-addon installation with this modified MAIN preserves its own initializer in the old startup path but has it replaced in the new one. This is a verified activation regression and a concrete candidate explanation for the reported old/new behavior. It still does not identify the original invalid-object write behind the exact execute-at-1 exception.

Historical cross-check requested by the user: 5.0.0 (`f5e93d3`), the `release_5.0.2` and `release_5.0.3` tags, and 5.0.4 (`ede8377`) all have the `AddonCount() > 0` automatic-install condition. In 5.0.0, `CharacterExpansion::Install` already installs `Cok6LoadHook`, whose action patch replaces `f_InitSaveSystem` using the generated bytecode. The builder's Git blob is identical in all four versions and official 5.1.1: `07b2158a4c57d7fc2d0279e76e129e203296fef6`.

Therefore addon character expansion **did use an expanded/replaced initializer throughout these v5 releases**. When automatic expansion was inactive, the game's own `f_InitSaveSystem` still executed normally. Do not describe the old behavior as the function not running.

The automatic-install condition is not a universal prohibition: the C++ export `HookCrashers_InstallCharacterExpansion` and Lua global `InstallCharacterExpansion` already expose explicit installation in 5.0.0, forwarding to the same installer without their own addon-count check. A mod invoking either path could activate replacement even with zero registered addons. The tester's old configuration and API usage are unknown; the conditional-regression explanation applies to the default bootstrap path without such explicit calls. If the tester actually had registered addons or explicitly installed expansion in the working version, the old initializer could already have been overwritten, so that observation would not isolate the newly unconditional hook as the crash cause.

The minimal correction for the zero-addon regression is to gate the character-specific MAIN/save and lobby-selection patches on character expansion being required, while keeping the loader available for menu injection. Preserving custom MAIN initialization when addon characters are also registered is a separate, broader compatibility change requiring an explicit storage/ID policy.

The broader corrective design should preserve the original initializer and its custom fields, then apply only the required character-registry changes with an explicit character-ID/storage policy. Blindly taking maximum counts, adding Refracted-only defaults, or skipping a failing SetMember would not resolve the general compatibility problem.

## 5.1.2 activation correction

At the user's request, the local 5.1.2 correction changes menu bootstrap to `Cok6LoadHook::Install(false)`. This installs the loader for menu injection without activating character/save/portrait/selection patches. The existing `CharacterExpansion::Install` call uses the default `Install(true)`, enabling all the historical character patches even when the loader was already attached for menus. This also preserves explicit Lua/C++ expansion installs with zero addons; activation is not inferred solely from the registry count. Character activation is atomic, remains enabled through subsequent menu-only requests, and resets on removal. The bytecode generator is unchanged. No release has been published as part of this correction.

`tests/Cok6CharacterActivationTests.cpp` includes the production loader and links the real initializer builder, registry and shape-patching implementation. It accepts the stock and tester MAIN files as local fixtures; Detours and online-menu import are stubbed. Checks cover byte-for-byte unchanged MAIN in menu-only mode, dispatch of menu injection, explicit activation with zero addons, real generated initializer insertion with a registered addon, upgrade without duplicate attach, removal, and failed-commit/retry state. Passing these offline checks does not by itself prove the tester's execute-at-1 crash resolved in-game.

Local analysis artifacts (not committed game files): `outputs/tester-main-analysis/comparison.json`, `production-patch-reach.json`, and per-function FFDec exports under `actionscript/`. Semantic comparison totals are not counts of changed gameplay functions: the original contains optimized actions that the string-normalizing comparison does not fully normalize.

## Diagnostic candidate

The separate diagnostics branch adds the exact HookCrashers version, bounded raw memory samples for ECX/EDX/ESI/EDI/EBX and 64 stack words. Memory is copied with ReadProcessMemory so unreadable sample addresses do not recursively fault in the handler. Export-derived stack labels are explicitly marked as unverified function identities.

This candidate adds evidence gathering only. It does not suppress the exception, skip SetMember, substitute a dummy character, or alter save/network behavior. Its base is 5.1.2-dev, so comparison against official 5.1.1 must account for the intervening graphics changes.

For the next reproduction, retain `HookCrashersCrash.log`, `HookCrashers.log`, the mod/version and exact steps. A dump from the failing process would additionally allow reconstruction of the live string table, receiver and VM frames. Start by checking `[EDX+0x10]`, the receiver's type/refcount, the three operand type tags and the enclosing script; then find the write or release that invalidates that object.
