-- Logs the best score it starts from, then beats it once a second and commits.
function on_start()
    zaro.log("best", save.get("best_score"), "coins", save.get("coins"), "pilot", save.get("pilot"))
end

function on_tick(tick)
    if tick % 60 == 0 then
        save.set("best_score", save.get("best_score") + 1)
        save.commit()
    end
    if tick == 130 then
        save.set("music_volume", 0.25)  -- after the last commit: the volume follows, the stored save does not
    end
end
