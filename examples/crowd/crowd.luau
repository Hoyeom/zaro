-- Waves: every 10 seconds (600 ticks) the horde gets faster, up to 9 m/s.
-- Edit and save this file while `zaro run examples/crowd/scene.toml` runs: it reloads live.

local WAVE_TICKS = 600
local SPEEDUP = 0.5
local MAX_SPEED = 9.0

local base_speed = 0

function on_start()
    base_speed = zaro.get("horde", "CrowdSpawner", "speed")
    zaro.log("wave 0, speed", base_speed)
end

function on_tick(tick)
    if tick % WAVE_TICKS == 0 then
        local wave = tick // WAVE_TICKS
        local speed = math.min(base_speed + wave * SPEEDUP, MAX_SPEED)
        zaro.set("horde", "CrowdSpawner", "speed", speed)
        zaro.log("wave", wave, "speed", speed)
    end
end
