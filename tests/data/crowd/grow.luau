-- Grows the horde by 150 every second up to its max, asks for more than max (the app keeps the
-- count at max), then retires most of it (D-034).
function on_tick(tick)
    local count = zaro.get("horde", "CrowdSpawner", "count")
    if tick % 60 == 0 and tick <= 360 then
        zaro.set("horde", "CrowdSpawner", "count", count + 150)
    elseif tick == 420 then
        assert(count == 1000, "six waves of 150 reach the max")
        zaro.set("horde", "CrowdSpawner", "count", 5000)
    elseif tick == 421 then
        assert(count == 1000, "a count above max is kept at max")
        zaro.set("horde", "CrowdSpawner", "count", 200)
    elseif tick > 421 then
        assert(count == 200, "the retired enemies stay gone")
    end
end
