# Four graphics presets — 5.1.2

| Runtime tex_quality | Label | Texture sizing |
| --- | --- | --- |
| 0 | Low | Current native policy |
| 1 | Medium | Current native policy |
| 2 | High | Historical independent per-axis bounds |
| 3 | Ultra | Existing Ultra rendering, moved from 2 |

High is inserted after Medium using the native localized option builder and
the existing High/Alta localization ID `0x31E`. Ultra remains subject to the
game's original `IsWow64Process` availability check. On a 32-bit OS, High is
available and is the maximum/default. The Ultra warning follows value 3.

## Historical reference

Steam app `204360`, depot `204361`, manifest `615242043503966810` dated
31 July 2024 was downloaded through the owner's authenticated SteamCMD account.
Original SHA-256:
`6e3cb44d04647ee219517a1142d3aea364efb0a60ed73122c597fc9a0a781e16`.
The commercial 2.x version number is not confirmed by the executable resources.
The historical game was never executed. A separate static analysis copy was
prepared with Steamless; neither game binary is distributed with this change.

Historical VA `459CA0` rounds a dimension to a power of two when requested,
then independently clamps each axis to the renderer minimum and maximum 512
for High. In ordinary use the minimum is 100. The port bounds rounding to avoid
uint32 overflow on invalid huge inputs. It does not apply Ultra's scale factors.

For example, a 400x800 request without power-of-two rounding and threshold 512
produces 400x512 with historical High and 256x512 with the current Medium
policy (minimum 100). This is a code-level example, not a claim about a
particular game's texture or total memory use. New art and other renderer
changes remain; the whole historical renderer is not transplanted.

## Configuration

The native INI retains `video/tex_quality`, now 0..3, and gains
`video/hookcrashers_quality_schema=1`. Without that marker, legacy 2 means Ultra
and is migrated to 3. Both the normalized value and schema are updated in the
same in-memory INI before native persistence. With the marker, 2 means High and
is not migrated again. Unrelated INI keys and player saves are unaffected.

This schema belongs to HookCrashers >=5.1.2. An older/unmodified executable
cannot distinguish a newly saved High value from its own Ultra value.

## Native integration

Install during initial ASI attach, before the executable entry point reads
settings. All sites are checked before any write; failure disables the feature.

- Platform global RVA `3610F0`, quality at object+`258`.
- Native INI read RVA `B7730`: ECX=this, four stack slots, `RET 16`.
- Native INI write RVA `B7810`: ECX=this, six stack slots, `RET 24`.
- Texture raster sizing RVA `F9F60`: ECX=this, six stack slots, `RET 24`.
- Localized option append RVA `154E50`: ECX=this, one stack slot, `RET 4`.
- Menu call site RVA `13544B`; atlas comparison `31375`; raster comparison
  `F9F84`; minimum setup/apply `F7995` / `1361D5`; warning `1363D5`;
  constructor, INI/reset and UI defaults `E051E`, `E0F56`, `E2438`, `135E79`;
  loader bounds `E0F72` / `E0F77`.

The INI functions include reserved stack slots in their callee cleanup. Their
decompiler's incomplete prototypes must not be copied as cdecl signatures.

## Offline verification

Generate the private fixture with `tests/extract_graphics_fixture.py` from the
supported current snapshot and historical analysis copy. No proprietary fixture
is committed. `GraphicsQualityTests.cpp` executes only isolated native sizing
routines, never launches the game, and compares 6,656 cases across small/large
dimensions, both rounding modes and multiple native thresholds.

The tests also exercise the production configuration callbacks and localized
High insertion, and attach/detach the real hook installation to a private mapped
snapshot. Corrupted instructions reject the feature without other image writes;
shutdown restores all patch sites and hook entries. This does not establish
end-to-end visual fidelity or gameplay performance.
