-- Tweens from a script (tests/test_script.cpp, test_tweens). The test steps the tweens after each
-- tick, as the app does.
local done_tick = 0
local blip = 0

function on_start()
    zaro.tween("dot", "Sprite", "scale", 3.0, {
        time = 1.0,
        done = function() done_tick = zaro.tick() end,
    })
    blip = zaro.spawn("blip") -- its prefab tween grows it from 0 to 1 in 0.25 s
    local ok, err = pcall(zaro.tween, "dot", "Transform", "rotation", 90, { time = 1 })
    assert(not ok and string.find(err, "gameplay state"), err)
    ok, err = pcall(zaro.tween, "dot", "Sprite", "scale", 2, { time = 1, ease = "out_cubc" })
    assert(not ok and string.find(err, "did you mean 'out_cubic'"), err)
end

function on_tick(tick)
    if tick == 31 then
        local scale = zaro.get("dot", "Sprite", "scale")
        assert(math.abs(scale - 2.0) < 1e-5, "halfway at tick 30: " .. scale)
        assert(zaro.get(blip, "Sprite", "scale") == 1, "the prefab tween finished")
        -- The file's colour tween ran 0.5 s up (red) and is on its way back.
        assert(zaro.get("dot", "Sprite", "color") == "#ff0000ff", zaro.get("dot", "Sprite", "color"))
    elseif tick == 61 then
        assert(done_tick == 60, "done ran in the tick the tween finished: " .. done_tick)
        assert(zaro.get("dot", "Sprite", "scale") == 3)
        assert(zaro.get("dot", "Sprite", "color") == "#000000ff", "pingpong twice ends where it started")
        local h = zaro.tween("dot", "Sprite", "scale", 5.0, { time = 1.0 })
        assert(zaro.cancel(h) and not zaro.cancel(h))
        zaro.tween("dot", "Sprite", "scale", 7.0, { time = 1.0 })
    elseif tick == 62 then
        zaro.set("dot", "Sprite", "scale", 1.5) -- stops the tween on this field
    elseif tick == 70 then
        assert(zaro.get("dot", "Sprite", "scale") == 1.5)
    end
end
