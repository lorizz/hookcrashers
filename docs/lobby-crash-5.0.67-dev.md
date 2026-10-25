# Lobby crash investigation: 5.0.67-dev

## Regression introduced by the no-Painter-DLC fallback

Commit `dbf1332` adds a direct call from the SWF dispatcher to the character
unlock native for addon indices. It declares that call as `__cdecl`.
The original target (`0xF0B60`) is inside another function's branch operand;
5.0.67-dev corrects the target to `0xF0BD0` but retains the wrong convention.

The supported x86 native at RVA `0xF0BD0` has three exits, all `RET 8`
(`0xF0BF6`, `0xF0C32`, `0xF0C47`). It receives two stack arguments and
removes both itself. The correct callable type is
`uint8_t (__stdcall*)(uint32_t player, uint32_t characterIndex)`.
The native returns a Boolean in AL. A cdecl caller additionally removes eight
bytes, unbalancing its stack and potentially restoring registers from the
wrong slots. Correcting only the entry address does not correct this ABI.

The tester crash at `0x111EE5` is `MOV byte ptr [EAX+8],0`, with EAX=-1.
This is the argument cleanup loop of the AVM1 CallFunction action, after an
indirect call to the dispatcher. Its local argument pointer and count are
invalid in the report. The recursive CallMethod/dispatcher frames are
consistent with this failure occurring while evaluating lobby script.
The ABI error is proven; attributing every byte of this particular corrupted
frame to it still requires gameplay validation with the corrected build.

## Separate receiver defect

The custom character sync code is unchanged by `dbf1332`. Its heap-backed
packet and HC5 header are separate from the newly added unlock fallback.
However, `ApplyExpandedPacketToRoster` calls RVA `0x12B0E0` to insert the
remote network ID into the runtime node's set at `node+0x38`.

That RVA is inside the six-byte indirect LeaveCriticalSection call beginning
at `0x12B0DC`, in an unrelated function starting at `0x12B070`.
It is not an entry point. The vanilla roster receiver at RVA `0x876F0`
performs the same insertion at call site `0x87A3D`, targeting RVA `0x2B030`.
This function takes ECX=the set at node+0x38 and four stack arguments
(output pair, search key, insertion key, forwarded construction context),
returning with RET 16. The corrected hook uses that entry and checks its
prologue/return before installing network hooks.

This defect predates the no-DLC commit (blame: `4dd3b394`), so it must not be
presented as the newly introduced regression. It can independently break a
receiver that reaches the matching base/addon insertion path.

## Offline verification

No game process is launched. `test_supported_executable.py` checks the real
snapshot's dispatcher target, native cleanup, receiver call target, invalid
old targets, crash instruction, and executable compatibility signatures.

`extract_lobby_fixture.py decrypted.exe fixture.bin` extracts only the two
isolated natives for `LobbyNativeAbiTests.cpp` (compile as MSVC Win32/x86).
The executable replaces the unlock native's save lookup/global with local
test dependencies and retains its real addon/no-save body and return paths.
It reproduces the old caller's eight-byte imbalance, then executes 100000
correct stdcall calls over four players and checks return values/frame guard.
It also exercises the real roster insert entry for an existing ID and a new
ID, replacing the tree allocation/insertion dependency for the latter.
These tests establish the native ABI/target fixes; they do not simulate a
full Steam session, streaming, or remote gameplay.

Do not commit or distribute the decrypted executable or binary fixtures.
