-- One call of each zaro.* function. Every 5 seconds the player gets 0.5 m/s faster.
-- The HUD shows the speed through the view-model value "speed" ({speed} in strings/*.toml). The
-- settings popup opens and closes by itself (open:settings and close actions); this script only
-- handles its choices.

local function show_speed()
    ui.set("speed", zaro.get("player", "PlayerController", "speed"))
end

function on_start()
    zaro.log("hello, arena seed", zaro.get("arena", "CrowdArena", "seed"))
    zaro.log("horde has", zaro.get("horde", "CrowdSpawner", "count"), "enemies")
    zaro.log("player exists:", zaro.has("player"), "| boss exists:", zaro.has("boss"))
    ui.set("sound", "on")
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

-- The chosen button of each pair is blue, the other grey.
local function show_choices()
    local sound_on = ui.get("sound") == "on"
    ui.set_field("settings", "sound_on", "Button", "color", sound_on and ON or OFF)
    ui.set_field("settings", "sound_off", "Button", "color", sound_on and OFF or ON)
    local korean = ui.get("language") == "ko"
    ui.set_field("settings", "korean", "Button", "color", korean and ON or OFF)
    ui.set_field("settings", "english", "Button", "color", korean and OFF or ON)
end

-- Each open makes a new instance of the popup: fill it in before it appears.
function on_screen(event, screen)
    if event == "created" and screen == "settings" then
        show_choices()
    end
end

function on_ui(action, screen, node)
    if action == "sound_on" or action == "sound_off" then
        ui.set("sound", action == "sound_on" and "on" or "off")
        show_choices()
    elseif action == "language_ko" or action == "language_en" then
        ui.language(action == "language_ko" and "ko" or "en")
        show_choices()
    end
end
