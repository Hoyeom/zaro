-- A blip every second, the sfx group at half volume, and the music stopped at tick 240.
function on_start()
    audio.volume("sfx", 0.5)
end

function on_tick(tick)
    if tick % 60 == 0 then
        audio.play("blip", { volume = 0.8, pan = -0.5 })
    end
    if tick == 240 then
        audio.music(nil, 0.2)
        assert(audio.track() == nil, "no track after audio.music(nil)")
    end
end

function on_ui(action)
    if action == "louder" then
        audio.volume("music", audio.volume("music") + 0.1)
    end
end
