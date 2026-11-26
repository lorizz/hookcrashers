# Menu API

Build in-game menus from a mod without touching `menu.swf`, menu handler tables
or hotspot coordinates. HookCrashers creates real Castle Crashers menus: the
book background, the highlight color, keyboard/controller navigation, the menu
sounds, the fade transitions and the Back button all come from the game.

> **Status: experimental (5.2.0-dev).** The native layer was built from the
> disassembly of the supported executable and has not been tested in-game yet.
> Report problems with your `HookCrashers.log`.

Menus are available while the main menu (`MENU.cok6`) is loaded: title screen,
main menu and every menu reachable from it. They are not available during a
level.

## Quick start (Lua)

```lua
local settings = HC.Menu.Create("mymod.settings", "My Mod")

settings:AddLabel("about", "Version 1.0")
settings:AddToggle("hardcore", "Hardcore mode", false, function(enabled)
    -- enabled is a boolean
end)
settings:AddChoice("speed", "Speed", { "Slow", "Normal", "Fast" }, 2, function(index, text)
    -- index is 1-based, text is the option label
end)
settings:AddButton("reset", "Reset", function()
    settings:SetValue("speed", 2)
    settings:SetValue("hardcore", false)
end)
settings:AddSubmenu("more", "More options", "mymod.more")

local more = HC.Menu.Create("mymod.more", "More options")
more:AddButton("hello", "Say hello", function()
    HC.CreatePrompt("Hello from My Mod!")
end)

settings:OnClose(function()
    -- called when the player presses Back
end)

-- Open from code: a callback, a prompt answer, or at startup.
settings:Open()
```

Every function returns `false` (or `nil`) on failure and writes the reason to
`HookCrashers.log`.

### Lua reference

| Function | Description |
|---|---|
| `HC.Menu.Create(id, title)` | Creates a menu and returns a menu object, or `nil`. |
| `HC.Menu.Get(id)` | Returns the menu object of an existing menu, or `nil`. |
| `menu:AddButton(itemId, label, fn)` | Button; `fn()` runs after the confirm flash. |
| `menu:AddToggle(itemId, label, initial, fn [, offLabel, onLabel])` | On/Off selector; `fn(enabled)`. Labels default to `Off`/`On`. |
| `menu:AddChoice(itemId, label, options, initialIndex, fn)` | Option selector; `fn(index, text)`, index is 1-based. |
| `menu:AddSubmenu(itemId, label, targetMenuId)` | Opens another menu; Back returns here. |
| `menu:AddLabel(itemId, text)` | Non-selectable line of text. |
| `menu:AddGraphic(itemId, graphicId, x, y)` | Places a registered graphic (see below). |
| `menu:OnClose(fn)` | `fn()` when the player leaves the menu with Back. |
| `menu:SetBackground(enabled)` | Shows/hides the book background (default: shown). |
| `menu:Open()` | Returns `ok, status`; status is `opened` or `queued`. |
| `menu:SetText(itemId, text)` | Changes a label, also while the menu is visible. |
| `menu:SetValue(itemId, value)` | Toggle: boolean. Choice: 1-based index. |
| `menu:GetValue(itemId)` | Toggle: boolean. Choice: `index, text`. |
| `menu:SetEnabled(itemId, enabled)` | Greys out a selectable item. |
| `HC.Menu.Open(id)` / `HC.Menu.Close()` | Open by id / close the current mod menu. |
| `HC.Menu.Current()` | Id of the mod menu on screen, or `nil`. |
| `HC.Menu.RegisterGraphic(graphicId, swfPath, exportName)` | Registers a menu graphic; the path is relative to the mod folder. |

All `menu:` functions can also be called as `HC.Menu.AddButton(menuId, ...)`.

## Quick start (C++ ASI mods)

```cpp
#include <HookCrashers.h>

static void __stdcall OnSpeed(const char* menuId, const char* itemId, int value, void*) {
    // value is the 0-based option index
}

static void __stdcall OnDone(const char*, const char*, int, void*) {
    HookCrashers::CloseMenu();
}

HOOKCRASHERS_MOD_EXPORT bool __stdcall HookCrashersModInit() {
    const HookCrashers::Menu menu("mynative.settings");
    menu.Create("My Native Mod");
    static const char* speeds[] = {"Slow", "Normal", "Fast"};
    menu.AddChoice("speed", "Speed", speeds, 3, 1, OnSpeed);
    menu.AddButton("done", "Done", OnDone);
    menu.Open(); // MenuOpenResult::Queued until the main menu exists
    return true;
}
```

Callback values: buttons and close `0`, toggles `0`/`1`, choices the 0-based
option index. Callbacks run on the game thread; keep them short.

## Graphics from your SWF

```lua
HC.Menu.RegisterGraphic("mymod.logo", "gfx/logo.swf", "logo")
settings:AddGraphic("logo", "mymod.logo", 400, 120)
```

* Register graphics while your scripts load. HookCrashers copies them into
  `MENU` when it loads; a graphic registered later appears after the next
  return to the title screen.
* `exportName` is the linkage name of a MovieClip ("Export for ActionScript").
  For SWFs without linkage names use `#<id>` (the sprite's character id).
* The SWF must be uncompressed (`FWS`), ActionScript 2, and contain vector art:
  shapes, morph shapes, sprites, fonts and dynamic text. Bitmaps, buttons, static
  text, video and `PlaceObject3` are rejected with a clear message, because
  their character references cannot be remapped safely yet.
* `x`, `y` place the clip's origin in the same space the stock menus use: 0,0
  is the top-left corner and the centre is half of the display configuration's
  design width/height (the stock menus place their full-screen clips at the
  centre).

## Limits

| Limit | Value |
|---|---|
| Mod menus per session | 16 (menu table slots 48–63) |
| Text items per menu (buttons, toggles, choices, submenus, labels) | 14 |
| Graphics per menu | 12 |
| Options per choice | 32 |
| Id length / characters | 64; letters, digits, `_ . - :` |
| Text length | 256 bytes of UTF-8 |

Text items are laid out automatically: centred, 30 px apart, around the middle
of the screen, with the title at the top.

Adding items after a menu has been shown rebuilds it the next time it opens
(the old native objects are not freed; avoid rebuilding in a loop). Text, value
and enabled changes are applied in place.

`Open` called outside the game thread (for example from a script at startup or
from your own thread) is queued and shown as soon as the main menu is built,
or on the next frame of a visible mod menu.

## How it works

1. **Menu table** – the main-menu builder (RVA `0x1442F0`) allocates the handler
   table. HookCrashers patches its three immediates (`0x1443E1`, `0x144414`,
   `0x144417`) from 46 to 64 entries after verifying the original bytes. Slots
   46/47 are the online selector and lobby browser, 48–63 are for mod menus.
2. **Building** – each mod menu becomes a native `MenuState` (0x20C bytes)
   filled with the game's own item classes, added through
   `FinalizeAndAddElement` (`0x1523E0`), which also performs the automatic
   layout.
3. **Callbacks** – the game calls item callbacks as `__cdecl callback(owner)`
   with the main menu as owner. HookCrashers installs one trampoline per action
   (confirm, value change, back, per-frame update) and finds the item from the
   current handler and its selected index.
4. **Graphics** – `MenuGraphicAssets` imports the registered sprites into the
   `MENU` SWF before the game parses it, with fresh character ids, and exports
   them as `hcg_<n>`; `GraphicMenuItem` instantiates them by that name.

## Native reference (build `0x6A4D42BF`, 2026-07-07)

All functions verified from the disassembly; `ret N` gives the stack bytes the
callee cleans.

| RVA | Function | Convention |
|---|---|---|
| `0x12C4C0` | `SetMenuState(main, i16 id, state)` | thiscall, `ret 8` |
| `0x1523E0` | `FinalizeAndAddElement(state, item)`, x/y in `xmm2`/`xmm3` (0/0 = keep or auto layout) | thiscall, `ret 4` |
| `0x153EE0` | `TextMenuItem(item, owner, callback, locId, maxWidth, selectable)`, 0x50 bytes, vtable `0x1D0B08` | thiscall, `ret 0x14` |
| `0x154780` | `TitleMenuItem(item, locId, unused)`, 0x50 bytes, vtable `0x1D0B68`, y = 75 | thiscall, `ret 8` |
| `0x154900` | `SelectorMenuItem(item, owner, onChange, labelLocId, selectable)`, 0x68 bytes, vtable `0x1D0B38` | thiscall, `ret 0x10` |
| `0x154E50` | `Selector::AddOption(locId)` | thiscall, `ret 4` |
| `0x155280` | `Selector::AddRawOption(const char*)` (ASCII via `mbstowcs`) | thiscall, `ret 4` |
| `0x154210` | `TextMenuItem::SetLocalizedText(locId, maxWidth)` | thiscall, `ret 8` |
| `0x1556C0` | `GraphicMenuItem(item, scene, exportName, depth, x, y)`, reuses an existing instance at the same depth | thiscall, `ret 0x14` |
| `0x153D70` | base item constructor: autoLayout +0x2C = 1, disabled +0x2D = 0, visible +0x2F = 1 | fastcall |
| `0x1533E0` | menu confirm: selectors cycle forward and call `+0x5C(owner)`; text items flash and queue action 1 | — |
| `0x153230` | menu move: 0 up, 1 down, 2/3 left/right on selectors (`+0x62`, wrap `+0x66`, then `+0x5C(owner)`) | — |
| `0x152E50` | action 1: `state+0x1B0(owner)` or `item+0x38(item+0x34)` | — |
| `0x152E90` | action 2 (Back): `state+0x1B4(owner)` | — |
| `0x153AD0` | runs the queued action after the fade-out and starts the fade-in | — |
| `0x12DFC0` | stock Back: `SetMenuState(current+0x1DA)` | cdecl |

`MenuState` fields used: item counts `+0x04/+0x06/+0x08/+0x0A`, selected index
`+0x0C`, transition flags `+0x12`, keep-selection `+0x14`, input enabled `+0x15`,
pending action `+0x18`, arrays (`+0x1C` hotspots, `+0x44` items, `+0x144`
graphics, `+0x184` buttons), owner `+0x1AC`, confirm override `+0x1B0`, Back
`+0x1B4`, extra buttons `+0x1B8..+0x1C4`, per-frame update `+0x1C8`, init
`+0x1D0`, pre-update `+0x1D4`, return menu id `+0x1DA`.

Input: the menu handles keyboard/controller only while `+0x15` is set; the
fade-in (`0x1539E0`, `+0x201` = 1, alpha `+0x204`) sets it when it completes,
and Confirm/Back clear it. HookCrashers starts that fade-in whenever a mod menu
becomes current.

Item fields: position `+0x04/+0x08`, alpha `+0x0C`, color `+0x10`, fallback
color `+0x14`, autoLayout `+0x2C`, disabled `+0x2D`, selectable `+0x2E`, visible
`+0x2F`, owner `+0x34`, callback `+0x38`, text `+0x3C` (game-allocated
`wchar_t*`), font scale `+0x40`, fit scale `+0x44`, alignment `+0x48`, length
`+0x4A`, visual width `+0x4C`, max width `+0x4E`. Selector: options vector
`+0x50`, change callback `+0x5C`, option count `+0x60`, index `+0x62`, wrap `+0x66`.
