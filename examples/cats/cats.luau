-- Nabi walks between two points, sits for a moment at each end, then turns around.
-- The walk animation plays while she moves; `flip_x` mirrors the art (drawn facing right).

local LEFT, RIGHT = 26.0, 38.0
local SPEED = 2.0 / 60.0 -- metres per tick
local SIT_TICKS = 90

local dir = 1
local sitting = 0

function on_tick(tick)
    local pos = zaro.get("nabi", "Sprite", "position")
    if sitting > 0 then
        sitting -= 1
        if sitting == 0 then
            dir = -dir
            zaro.set("nabi", "Sprite", "flip_x", dir < 0)
            zaro.set("nabi", "Sprite", "animation", "walk")
        end
        return
    end
    local x = pos.x + dir * SPEED
    if (dir > 0 and x >= RIGHT) or (dir < 0 and x <= LEFT) then
        sitting = SIT_TICKS
        zaro.set("nabi", "Sprite", "animation", "idle")
    end
    zaro.set("nabi", "Sprite", "position", { x = x, y = pos.y, z = pos.z })
end
