# Current character data API — 5.1.0-dev

Implemented and tested offline; the real game lifecycle/player chains still
need in-game verification. No game process was launched during validation.

Include `HookCrashers.h`, link the matching Win32 `HookCrashers.lib`:

```cpp
#include <HookCrashers.h>

void CheckPlayerOne() {
    auto data = HookCrashers::GetCurrentCharacterData(); // Defaults to HUD slot 1
    if (data.result != HookCrashers::CharacterDataResult::Ok ||
        data.source != HookCrashers::CharacterDataSource::LivePlayer ||
        data.state != HookCrashers::CharacterDataState::InLevel) return;
    if (HookCrashers::HasCharacterField(data, HookCrashers::CurrentCharacterField::Health)) {
        const double health = data.health; // Preserves AS float precision
        // Pass health to the achievement integration.
        (void)health;
    }
}
```

Pass 2, 3 or 4 for the other HUD slots. Calling from any thread copies a cached
snapshot; it does not execute ActionScript or walk game pointers on that thread.
Before the first capture it returns `Unavailable`; `Stale` means older than 500 ms.

## Runtime Update callback

Register a `void __stdcall OnUpdate()` with
`HookCrashers::RegisterUpdateCallback(OnUpdate)` during mod initialization.
Inside it, call your own `void Update()` and read `GetCurrentCharacterData(slot)`.
See `examples/native/CharacterUpdate.cpp` for a four-player level-increase check.

Callbacks run once after each intercepted native update and publication of all
four snapshots, on that update's game thread. They also run for unavailable,
HUD-only and loading snapshots; check result/source/state/validFields first.
These are polling callbacks, not ActionScript level-up events: intermediate
changes between captures cannot be recovered. The example resets its baseline
on loading/unavailability or identity changes; changes during that gap are not
counted. The initial snapshot never counts as a level-up.

There are 16 callback slots. Null/duplicate registrations and a full registry
return false. Read APIs are allowed inside a callback. Register/unregister calls
from inside any Update callback return false to prevent a deadlock; schedule
those changes on an independent control thread. Keep callbacks short, do not
throw, and queue disk/network work elsewhere: the game update lock is still held.
A callback raising a Windows exception is disabled and logged; other callbacks
continue. Unregister it to remove its slot.

Before unloading the consuming DLL, call
`HookCrashers::UnregisterUpdateCallback(OnUpdate)` from outside the callback
and outside `DllMain`/the loader lock. A successful return waits for all active
invocations and prevents subsequent calls. Do not wait for that control thread
from Update. Registration does not pin the consuming DLL or manage its lifetime.
For HookCrashers-managed mods, use ModInit/ModShutdown as in the example.

C exports `HookCrashers_RegisterUpdateCallback(callback)` and
`HookCrashers_UnregisterUpdateCallback(callback)` use `bool __stdcall` with
the same no-argument `void __stdcall` callback type. Both have undecorated exports.

## ABI and identity (data)

Definitions: `sdk/include/CurrentCharacterData.h`. Undecorated C exports:

- `HookCrashers_GetCurrentCharacterData(playerSlot, output, outputSize)`
- `GetCurrentCharacterData`: alias of the same three-argument C export.
- `HookCrashers_GetCurrentCharacterProperty(playerSlot, expectedSequence,
  propertyIndex, output, outputSize)`

All use `__stdcall`. The C++ wrapper returns a value; the C export fills an output
buffer and returns `CharacterDataResult`. When using GetProcAddress, use the C
signature from the header, not the zero-argument C++ convenience signature.

Schema 1: CurrentCharacterData is 264 bytes, CurrentCharacterProperty is 240,
packing is 8. Smaller buffers are rejected; larger buffers receive the current
struct size only. `structSize` and `schemaVersion` identify the returned layout.

- `playerSlot`: HUD slot 1..4, distinct from controller/save port and character ID.
- `savePort`: zero-based ReadStorage port, derived from HUD.port-1. It is only
  valid when HUD.port is an integer in 1..4.
- `characterId`: raw ActionScript p_type, including addons. No conversion to a
  zero-based save index is performed.
- `validFields`: bits indexed by CurrentCharacterField. Missing is not zero.

Numeric core fields are doubles with individual validity: identity, alive,
health/max health, current/max magic, level, XP/next XP, gold, four stats,
weapon/repaint, pet type, consumables, equipped item, kills/resists, unspent
stat points and insane mode. Non-finite numbers do not receive a validity bit.
Pet type also checks player.pet.animal_type when there is no direct property.

## Capture and lifecycle

Capture runs after the native profile/frame update at RVA 0x1082D0. Its private
ECX/XMM1 input ABI is preserved, including x87/SSE state around our callback.
The game thread's existing update critical section remains held at this point.
Installation refuses a mismatched native signature.

Profile.currentMovie (+0x190) -> movie+0x70 gives the context; context+0x5C is
_root and context+0x2F2C its string table. Native GetMember resolves hud1..hud4.
HUD.player_pt is accepted only with a reciprocal player.hud_pt match. Otherwise
the explicit _root.loader.game.game.pN path is tried; a different HUD owner is
rejected. These chains are statically recovered and remain to be verified live.

When g_bLoading is true or no live player is found, source=Hud with an explicit
Loading/HudOnly/Inactive state. HUD data can be a saved transition copy; it is
not silently reported as live health. A dead player still in-level can have
valid live health=0 and alive=false. Removing the movie invalidates old data.

## Generic properties, arrays and mods

In addition to core fields, the API enumerates the selected object's own named
properties, including mod-defined fields. Arrays have indexed child records;
plain objects can have nested records. Movie/function/engine objects remain
opaque references rather than recursively pulling in the entire scene graph.

```cpp
auto data = HookCrashers::GetCurrentCharacterData(1);
if (data.result == HookCrashers::CharacterDataResult::Ok) {
    for (std::uint32_t i = 0; i < data.propertyCount; ++i) {
        HookCrashers::CurrentCharacterProperty p {};
        auto result = HookCrashers_GetCurrentCharacterProperty(
            1, data.sequence, i, &p, sizeof(p));
        if (result != HookCrashers::CharacterDataResult::Ok) break;
        // Read p.name, kind, number, text. Use parentIndex for nesting.
        // On SequenceChanged, discard the enumeration and restart.
    }
}
```

The sequence prevents mixing captures. parentIndex=UINT32_MAX means a direct
property; otherwise it references an earlier record. referenceId is local to
the snapshot (1 is the selected root object), never a process pointer. Cycles
and repeated objects are marked. Undefined/Boolean/Integer/Float/String/Object/
Array/Opaque remain distinguishable. Strings/names are copied narrow VM bytes
and NUL terminated.

Limits: 512 property records/player, 64 traversed objects, depth 3, 63 name
bytes and 127 text bytes. Truncation, depth limits, opaque references and read
faults are explicit. This is a bounded snapshot, not every reachable object or
every computed native property. It does not write VM variables, intern names,
modify string-table cursors, or retain VM pointers for external callers.

## Verification

The production reader/cache/export tests cover four players, wrong reciprocal
links, loading/inactive/dead states, independent IDs, floats/zero/missing fields,
attached pet, arrays, nested custom data, cycles, truncation, invalid buffers,
sequence mismatch, stale data, concurrent readers, scene removal and the assembly
bridge's ECX/XMM1 forwarding. Actual isolated native own-property getter, map
lookup, object-reference conversion, string hash and array-length getter are
also used. The private fixture has no game startup and must not be distributed.

Run `extract_current_character_fixture.py decrypted.exe fixture.bin` (also writes
fixture.frame.bin), compile
CurrentCharacterDataTests.cpp + Core/Memory.cpp + Core/Logger.cpp as MSVC x86
with Detours headers/library, then pass fixture.bin and fixture.frame.bin to the executable. Do not
compile CurrentCharacterData.cpp a second time: the test includes it directly.
The latter fixture only runs the native frame's disabled-update branch, with a
local cookie-helper stub: it verifies the real Detours attach/capture/detach path
without reaching game update callbacks, startup, assets or networking.
