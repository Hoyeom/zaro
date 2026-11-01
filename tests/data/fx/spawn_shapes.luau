-- Spawns an orb every two ticks for 20 ticks, moving right, and destroys every third one.
local orbs = {}

function on_tick(tick)
    if tick % 2 == 0 and tick <= 20 then
        orbs[#orbs + 1] = zaro.spawn("orb", { Shape = { position = { x = 22 + tick, y = 0.1, z = 32 } } })
        if #orbs % 3 == 0 then
            zaro.destroy(orbs[#orbs - 1])
        end
    end
end
