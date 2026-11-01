-- Parents move, turn and scale their children (tests/test_script.cpp, test_hierarchy).
local function near(a, b)
    return math.abs(a - b) < 1e-5
end

function on_start()
    -- The sun is at (10, 0, 0), turned 90 degrees (+x toward +z) and twice as big.
    local p, rotation, scale = zaro.world("planet")
    assert(near(p.x, 10) and near(p.y, 0) and near(p.z, 4), "planet at " .. p.x .. "," .. p.y .. "," .. p.z)
    assert(rotation == 90 and scale == 2)

    local moon = zaro.spawn("moon", { name = "moon", parent = "planet" })
    local m = zaro.world(moon)
    assert(near(m.x, 10) and near(m.z, 6), "moon at " .. m.x .. "," .. m.z)
    assert(zaro.parent(moon) == zaro.find("planet") and zaro.parent("sun") == nil)
    local kids = zaro.children("planet")
    assert(#kids == 1 and kids[1] == moon)

    -- An entity without a Transform sits at its parent's origin.
    zaro.set_parent("comet", "planet")
    local c = zaro.world("comet")
    assert(near(c.x, 10) and near(c.z, 4))
    zaro.set_parent("comet", nil)
    assert(zaro.parent("comet") == nil)

    local ok, err = pcall(zaro.set_parent, "sun", moon)
    assert(not ok and string.find(err, "already below"), err)
end

function on_tick(tick)
    if tick == 1 then
        zaro.set("sun", "Transform", "rotation", 180)
        local m = zaro.world("moon")
        assert(near(m.x, 4) and near(m.z, 0), "moon after the turn at " .. m.x .. "," .. m.z)
    elseif tick == 2 then
        zaro.destroy("sun")
        assert(not zaro.alive("planet") and not zaro.alive("moon") and zaro.alive("comet"))
    end
end
