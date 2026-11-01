-- A handler that emits its own event forever: ZE1201 stops it (tests/test_script.cpp).
function on_start()
    zaro.on("ping", function() zaro.emit("ping") end)
end

function on_tick(tick)
    if tick == 1 then
        zaro.emit("ping")
    end
end
