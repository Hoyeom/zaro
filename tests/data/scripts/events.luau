-- Timers and events (tests/test_script.cpp, test_events): the order below is part of the API.
local log = {}
local function add(entry)
    log[#log + 1] = entry
end

function on_start()
    zaro.after(1.0, function(tick) add("after@" .. tick) end) -- 60 ticks
    zaro.every(0.5, function(tick) add("every@" .. tick) end, 3) -- ticks 30, 60, 90
    zaro.on("hit", function(payload, event)
        add(event .. payload.n)
        if payload.n < 3 then
            zaro.emit("hit", { n = payload.n + 1 })
        end
    end)
    local never = zaro.on("hit", function() add("never") end)
    assert(zaro.cancel(never) and not zaro.cancel(never))
    zaro.emit("hit", { n = 1 }) -- delivered after on_start, with the two it causes
    zaro.on("engine", function(payload) zaro.set("horde", "CrowdSpawner", "speed", payload.speed) end)
end

function on_tick(tick)
    if tick == 60 then
        add("tick60")
    elseif tick == 91 then
        local got = table.concat(log, ",")
        assert(got == "hit1,hit2,hit3,every@30,after@60,every@60,tick60,every@90", got)
    end
end
