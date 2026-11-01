-- One call of each zaro.* function. Every 5 seconds the player gets 0.5 m/s faster.

function on_start()
    zaro.log("hello, arena seed", zaro.get("arena", "CrowdArena", "seed"))
    zaro.log("horde has", zaro.get("horde", "CrowdSpawner", "count"), "enemies")
    zaro.log("player exists:", zaro.has("player"), "| boss exists:", zaro.has("boss"))
end

function on_tick(tick)
    if tick % 300 == 0 then
        local speed = zaro.get("player", "PlayerController", "speed") + 0.5
        zaro.set("player", "PlayerController", "speed", speed)
        zaro.log("tick", zaro.tick(), "player speed", speed)
    end
end
