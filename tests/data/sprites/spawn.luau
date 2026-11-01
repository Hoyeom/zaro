-- Spawns three sparks in a row on tick 1 and destroys the middle one on tick 5.
function on_tick(tick)
    if tick == 1 then
        for i = 1, 3 do
            zaro.spawn("spark", { name = "s" .. i, Sprite = { position = { x = 26 + i * 3, y = 0, z = 32 } } })
        end
    elseif tick == 5 then
        zaro.destroy("s2")
    end
end
