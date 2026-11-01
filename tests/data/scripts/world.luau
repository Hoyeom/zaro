-- Spawns, queries and destroys entities (tests/test_script.cpp, test_world).
local grunts = {}

function on_start()
    for i = 1, 3 do
        grunts[i] = zaro.spawn("grunt", { CrowdSpawner = { speed = i } })
    end
    zaro.spawn("grunt", { name = "boss", PlayerController = { speed = 2 } })
    assert(zaro.count("CrowdSpawner") == 5, "horde and four grunts")
    assert(zaro.count("CrowdSpawner", "PlayerController") == 1, "only the boss has both")
    assert(zaro.name(grunts[1]) == "grunt#3", zaro.name(grunts[1]))
    assert(zaro.find("boss") == 6)
    assert(zaro.get(grunts[3], "CrowdSpawner", "speed") == 3)
    assert(zaro.get("boss", "PlayerController", "speed") == 2)
end

function on_tick(tick)
    if tick == 1 then
        assert(zaro.destroy(grunts[2]))
        assert(not zaro.alive(grunts[2]))
        assert(not zaro.destroy(grunts[2]), "a second destroy of the same id is false")
        assert(zaro.count("CrowdSpawner") == 4)
        local q = zaro.query("CrowdSpawner")
        assert(#q == 4 and q[1] == 1 and q[2] == grunts[1] and q[3] == grunts[3] and q[4] == 6)
    elseif tick == 2 then
        zaro.set(grunts[1], "CrowdSpawner", "speed", 9)
        assert(zaro.has(grunts[1], "CrowdSpawner") and not zaro.has(grunts[2]))
    end
end
