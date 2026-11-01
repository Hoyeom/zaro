-- One call of each zaro.* function. Every 5 seconds the player gets 0.5 m/s faster.
-- The HUD shows the speed through the view-model value "speed" ({speed} in strings/*.toml), and
-- on_ui opens and closes the settings popup and switches the language.

local function show_speed()
    ui.set("speed", zaro.get("player", "PlayerController", "speed"))
end

function on_start()
    zaro.log("hello, arena seed", zaro.get("arena", "CrowdArena", "seed"))
    zaro.log("horde has", zaro.get("horde", "CrowdSpawner", "count"), "enemies")
    zaro.log("player exists:", zaro.has("player"), "| boss exists:", zaro.has("boss"))
    show_speed()
end

function on_tick(tick)
    if tick % 300 == 0 then
        local speed = zaro.get("player", "PlayerController", "speed") + 0.5
        zaro.set("player", "PlayerController", "speed", speed)
        zaro.log("tick", zaro.tick(), "player speed", speed)
        show_speed()
    end
end

local ON, OFF = "#3a6ab0", "#787c88"

-- The chosen button of a pair is blue, the other grey.
local function choose(on_node, off_node)
    ui.set_field("settings", on_node, "Button", "color", ON)
    ui.set_field("settings", off_node, "Button", "color", OFF)
end

function on_ui(action, screen, node)
    if action == "open_settings" then
        ui.set_field("settings", "popup", "Layout", "visible", true)
    elseif action == "close" then
        ui.set_field("settings", "popup", "Layout", "visible", false)
    elseif action == "sound_on" then
        choose("sound_on", "sound_off")
    elseif action == "sound_off" then
        choose("sound_off", "sound_on")
    elseif action == "language_ko" then
        ui.language("ko")
    elseif action == "language_en" then
        ui.language("en")
    end
end
