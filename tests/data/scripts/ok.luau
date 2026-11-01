-- Doubles the horde speed on start and sets the count to 100 + tick on every tick.
function on_start()
    zaro.set("horde", "CrowdSpawner", "speed", zaro.get("horde", "CrowdSpawner", "speed") * 2)
end

function on_tick(tick)
    zaro.set("horde", "CrowdSpawner", "count", 100 + tick)
    if tick == 2 then
        zaro.log("tick", tick, "player has controller:", zaro.has("player", "PlayerController"))
    end
end
