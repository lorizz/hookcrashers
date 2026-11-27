# Save slots

With at least one mod installed, HookCrashers keeps up to 10 separate saves.
The vanilla `cc_save.dat` is never written while mods are loaded.

## Files (Steam Remote Storage, synced by Steam Cloud)

| File | Content |
|---|---|
| `hc_slotN.dat` / `.bak` | the save of slot N (same format as `cc_save.dat`, plus one record per addon character) |
| `hc_slotN.meta` | slot name, dates, HookCrashers version, addon character order, mods and versions, kept characters of removed mods |
| `hc_slotN.dat.old` | the slot file as it was before the last character rearrangement |
| `hc_slots.cfg` | the slot used last (`active=N`) |
| `hc_trash_slotN.*` | deleted slots |
| `hcpbp.dat` | PBP config, shared by all slots |

## Save format

`cc_save.dat` is Blowfish-ECB over 8-byte blocks with little-endian words.
The 24-byte key is the constant at RVA `0x1C4700` with the SteamID64 written
over it (`u16@0 = hi>>16`, `u16@0x13 = hi&0xFFFF`, `u16@5 = lo>>16`,
`u16@0xF = lo&0xFFFF`). A save copied from another Steam account does not decrypt.
The last dword of the plaintext is the checksum of native `0x130EE0`.

| Offset | Size | Content |
|---|---|---|
| `0x000` | `0x40` | global data (options, unlocked animals/items/arenas) |
| `0x040` | 32 × `0x30` | base characters |
| `0x640` | N × `0x30` | HookCrashers addon characters, in registration order |
| … | 10 × `0x30` | workshop characters |
| … | `0x2C` | keybinds |
| end − 4 | 4 | checksum |

## Character remapping

The metadata stores the addon character list the save was written with. Before
the game reads a slot, HookCrashers compares it with the characters now
registered and, if they differ, rebuilds the file (`src/Save/SaveLayout.cpp`):

- characters still installed keep their own record, wherever they move;
- characters of removed mods are stored in the metadata (`stash=`);
- they are put back when the mod returns;
- new characters get their default data;
- the workshop characters and keybinds move after the addons.

## Locked slots

Mods that register addon characters are stored in the metadata with a `save`
flag (`mod=Name\tversion\tsave`). When one of them is missing or has another
version, the slot is locked: the menu does not offer Play and the slot is
never loaded or remapped. Adding character mods is allowed (the new
characters get default data); mods without characters never lock a slot.

## Switching slots

The game reads the save once at startup. To change slot, HookCrashers points the redirected
filename at the new slot and calls the native `CC_ReadSaveFile` (RVA `0xE8E50`)
again from the menu, on the game thread. Before the call it puts a sentinel in
the game's save buffer; after it, the buffer must hold exactly the expected
bytes (or a newly created default save). Otherwise the switch is cancelled and
the previous buffer and filename are restored.

## Main menu button

`src/SWF/SaveSlotButtonAsset.cpp` changes the MENU movie in memory when it is
loaded. The main menu's `buttonchoices` sprite shows frame `selected + 1`;
items 3 (green Options) and 4 (red) share a row. The patch:

- adds recolored copies of the red button shape (green and teal) and a white
  save icon, then a new `buttonchoices` with three 81 px buttons, highlights on
  frames 4, 5 and 9, all appended before End with their own export (the stock
  export is renamed in place and `FileLength` is updated);
- `src/UI/MainMenuSlotButton.cpp` adds main-menu item 8 (item 7 is the
  version text) and its mouse hotspot, narrows the hotspots of items 3 and 4
  and replaces the menu pre-update so left/right cycle 3 -> 4 -> 8.

Every original value is checked first; with another game build nothing is
patched. `diagnostics\dump_menu` makes HookCrashers write every decrypted
movie to `diagnostics\swf_runtime\` (the SWF `FileLength` of MENU is stale,
the dump follows the tags to End).
