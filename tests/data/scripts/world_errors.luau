-- Each tick misuses the entity API once; tests/test_script.cpp checks the error and its line.
function on_tick(tick)
    if tick == 1 then
        zaro.spawn("grnut")
    elseif tick == 2 then
        zaro.get("playr", "PlayerController", "speed")
    elseif tick == 3 then
        zaro.get(99, "PlayerController", "speed")
    elseif tick == 4 then
        zaro.spawn("grunt", { CrowdSpawnr = {} })
    elseif tick == 5 then
        zaro.spawn("grunt", { name = "player" })
    end
end
