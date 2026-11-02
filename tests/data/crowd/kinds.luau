-- Two kinds of enemy (D-036). A sweep in front of the player takes only runners, one behind it
-- takes any kind and says how many of each; crowd_hit events say which kind reached the player.
-- Every two seconds there are 200 more drones and 50 more runners.
local hits = { drones = 0, runners = 0 }
local swept = { drones = 0, runners = 0 }

function on_start()
    local ok, err = pcall(zaro.crowd_kill, 0, 0, 1, 0, "player")
    assert(not ok and string.find(err, "is not a kind of the crowd"), "only a spawner is a kind")
    zaro.on("crowd_hit", function(hit)
        local name = zaro.name(hit.kind)
        hits[name] += hit.count
    end)
end

function on_tick(tick)
    local p = zaro.world("player")
    local n, only = zaro.crowd_kill(p.x + 2, p.z, 1.5, 0, "runners")
    assert(only.drones == nil and n == (only.runners or 0), "a sweep of runners takes only runners")
    local m, all = zaro.crowd_kill(p.x - 2, p.z, 1.5)
    assert(m == (all.drones or 0) + (all.runners or 0), "the kinds add up to the count")
    swept.drones += all.drones or 0
    swept.runners += n + (all.runners or 0)
    if tick % 120 == 0 then
        zaro.set("drones", "CrowdSpawner", "count", zaro.get("drones", "CrowdSpawner", "count") + 200)
        zaro.set("runners", "CrowdSpawner", "count", zaro.get("runners", "CrowdSpawner", "count") + 50)
    end
    if tick == 600 then
        zaro.log("hits", hits.drones, hits.runners, "swept", swept.drones, swept.runners)
        assert(hits.drones > 0 and hits.runners > 0, "both kinds reached the player")
        assert(swept.drones > 0 and swept.runners > 0, "both kinds were swept")
    end
end
