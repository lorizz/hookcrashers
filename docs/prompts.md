# Prompt API

HookCrashers exposes the native `bookbg` Yes/No prompt to Lua and C++ mods.
Only one mod prompt can be active or pending at a time. `CreatePrompt` returns
`false` when the message is empty or another mod prompt already owns the menu.

## Lua

```lua
local function OnConfirm()
    -- YES was pressed.
end

local function OnDecline()
    -- NO was pressed.
end

CreatePrompt("MessageHere", OnConfirm, OnDecline)
```

The same function is available as `HC.CreatePrompt(...)` and
`HC.UI.CreatePrompt(...)` after `local HC = require("HookCrashers")`.
Callbacks are optional and execute on the game thread after the prompt closes.
Back is disabled while the prompt is active.

## C++

Include `src/UI/PromptApi.h` when building against the HookCrashers source API:

```cpp
void __stdcall OnConfirm(void* userData) {
    // YES was pressed.
}

void __stdcall OnDecline(void* userData) {
    // NO was pressed.
}

HookCrashers::UI::CreatePrompt(
    "MessageHere", OnConfirm, OnDecline, nullptr);
```

Binary mods can resolve the equivalent exported ABI function:

```cpp
HookCrashers_CreatePrompt(
    "MessageHere", OnConfirm, OnDecline, nullptr);
```

Call the C++ API from the game thread. Callback function pointers and `userData`
must remain valid until either Yes or No is selected.
