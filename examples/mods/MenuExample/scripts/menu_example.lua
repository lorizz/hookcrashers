-- HookCrashers Menu API example (docs/menu-api.md).
-- Like every folder mod, installing this example makes HookCrashers use a
-- separate save (hc_menuexample.dat); your cc_save.dat is not touched.

local main = HC.Menu.Create("menuexample.main", "Menu Example")
local options = HC.Menu.Create("menuexample.options", "Options")

main:AddLabel("intro", "Built with the HookCrashers Menu API")
main:AddButton("greet", "Say hello", function()
    HC.CreatePrompt("Hello from the Menu Example!")
end)
main:AddSubmenu("options", "Options", "menuexample.options")
main:AddButton("close", "Close", function()
    HC.Menu.Close()
end)

local clicks = 0
options:AddToggle("sparkles", "Sparkles", true, function(enabled)
    options:SetText("status", enabled and "Sparkles are on" or "Sparkles are off")
end)
options:AddChoice("pace", "Pace", { "Relaxed", "Normal", "Frantic" }, 2, function(index, text)
    options:SetText("status", "Pace: " .. text .. " (" .. index .. ")")
end)
options:AddButton("count", "Count clicks", function()
    clicks = clicks + 1
    options:SetText("count", "Clicked " .. clicks .. " times")
end)
options:AddLabel("status", "Change an option")

main:OnClose(function()
    -- Back from the example returns to the menu that was open before it.
end)

-- Shown as soon as the main menu is ready. A real mod would open its menu
-- from a prompt, another menu or its own logic instead.
main:Open()
