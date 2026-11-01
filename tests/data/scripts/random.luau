-- math.random must follow the seed the script was loaded with.
function on_start()
    zaro.set("horde", "CrowdSpawner", "count", math.random(1, 1000000))
end
