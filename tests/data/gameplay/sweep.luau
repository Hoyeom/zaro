-- A blade orbits the player and sweeps enemies away; enemies that reach the player are counted.
kills, hits, last_hit = 0, 0, nil

function on_start()
    zaro.on("crowd_hit", function(hit)
        hits += hit.count
        last_hit = hit
    end)
end

function on_tick(tick)
    zaro.set("player", "Transform", "rotation", tick * 6)
    local w = zaro.world("blade")
    kills += zaro.crowd_kill(w.x, w.z, 1.2)
    if tick == 600 then
        assert(kills > 0, "the blade swept enemies away")
        assert(hits > 0 and last_hit.x ~= nil and last_hit.z ~= nil, "crowd_hit events arrived")
        local p = zaro.world("player")
        assert(p.x ~= 0 or p.z ~= 0, "the player Transform follows the simulated player")
        zaro.log("kills", kills, "hits", hits)
    end
end
