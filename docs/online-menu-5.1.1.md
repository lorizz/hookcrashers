# Online menu restoration and host game mode — 5.1.1-dev

Development-channel build; do not confuse a successful offline test with a
two-account Steam gameplay test.

## Restored injected menus

The native menu integration and original artwork were recovered from the parent
of `517b3e1` (the v5 refactor which removed them). This is the game's injected
menu: custom SWF definitions are imported into MENU before parsing, and native
menu states 46/47 own the selector and lobby screen respectively.

- `Play Vanilla` follows the original game's Play With Friends transition.
- `Play Modded` opens the injected `playerlobbies` screen.
- Back from that screen returns to the injected compatibility selector.

The original SWFs are preserved byte-for-byte under `assets/menu/` and embedded
as RCDATA in the ASI. No external modified menu.pak is required. The new importer
remaps definition IDs and edit-text font references, retains the entire physical
COK6 MENU (whose legacy SWF FileLength is smaller), and refuses malformed inputs
without publishing a partial buffer.

The lobby-list backend/real-lobby joining are not part of this restoration.
The old `/test-lobbies` provider was demonstrative. The screen starts empty;
design-time sample names and placeholder text are not presented as real lobbies.
No new restart-without-mods flow is introduced. The restored Vanilla entry is
the original native menu path; it does not hot-unload mods or undo expanded saves.

## Classic/Fresh is decided by the host

Expected behavior is **alignment**, not rejection:

| Host | Client before joining | Client after sync |
| --- | --- | --- |
| Classic | Classic | Classic |
| Classic | Fresh | Classic |
| Fresh | Classic | Fresh |
| Fresh | Fresh | Fresh |

The previous expanded sender wrote `1` unconditionally in the network byte
which the native sender fills with the Fresh setting. The custom receiver also
skipped the original native setter. Thus local graphics modes could remain
different despite successful addon character synchronization.

Ghidra evidence on the supported decrypted executable:

- Native sender RVA `F216F..F21AA`: profile global `391294`, setting map at
  profile+`814`, key `0120`, value at tree node+`14`.
- Native receiver RVA `F2390`: before building/applying roster state, apply
  the incoming mode when self+`29A4` is enabled, peer vfunc+`20` identifies the
  authoritative peer, and session vfunc+`68` does not indicate local authority.
- Native setter RVA `10A540`, `void __stdcall(uint8_t)` (`RET 4`), stores the Boolean in
  setting `0120`. The former NativeDB RVA `4A24C` was incorrect.

The expanded sender now advertises the actual Classic/Fresh setting. The
receiver validates the HC envelope first and then follows the same authority
predicate and setter as vanilla, **before** applying roster data or setting
self+`29A5` ready. A client packet cannot change the host's setting. Native
packets continue through the original native handler.

HC wire version is **6** because v5's hardcoded mode is unreliable. All modded
peers must use this protocol (5.1.1-dev) and matching addon layout. Recognized HC
packets with an old/wrong version, invalid mode or incompatible layout are
rejected, rather than misinterpreted as vanilla. This rejection concerns bad
protocol metadata, not a valid Classic/Fresh difference.

## Validation

`LobbyGameModeTests.cpp` exercises the production sender/parser/receiver with a
native-layout setting tree, peer/session vtables and roster model: all four
mode combinations, host authority, native sync predicate, actual wire mode,
old/malformed metadata without mode mutation, and native fallback.

`OnlineMenuAssetsTests.cpp` imports both original SWFs into the user-provided
physical MENU and tests truncation/atomic failure. Proprietary MENU/executable
fixtures stay outside the repository.
