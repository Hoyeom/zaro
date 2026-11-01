-- examples/neon gameplay (D-031). Blades orbit the player and sweep the horde away; every few
-- seconds a wave makes the horde faster and sends golden elites, which only the blades can stop.
-- Uses what P6 gives scripts: spawning and destroying, a hierarchy (blades on a rotor on the
-- player), events, timers and tweens. Edit and save while `zaro run examples/neon/scene.toml` runs.

local WAVE_SECONDS = 8
local SPEEDUP = 0.25 -- horde m/s per wave
local MAX_SPEED = 7.0
local MAX_BLADES = 6
local ROTOR_DEGREES = 7 -- per tick
local SWEEP_RADIUS = 1.0 -- metres around each blade's tip
local ELITE_SPEED = 3.0 / 60 -- metres per tick
local ELITE_HIT = 1.4 -- a blade this close takes an elite down
local ARENA = 96

local blades = {}
local elites = {}
local wave = 0
local score, combo = 0, 0

local function add_blade()
    if #blades >= MAX_BLADES then
        return
    end
    blades[#blades + 1] = zaro.spawn("blade", { parent = "rotor" })
    for i, blade in ipairs(blades) do
        zaro.set(blade, "Transform", "rotation", (i - 1) * 360 / #blades)
    end
end

-- A ring that grows and fades where something was taken down, then goes away.
local function shockwave(x, z, color)
    local ring = zaro.spawn("shock", { Shape = { position = { x = x, y = 0.08, z = z }, color = color } })
    zaro.tween(ring, "Shape", "size", { x = 7, y = 7 }, { time = 0.45, ease = "out_cubic" })
    zaro.tween(ring, "Shape", "intensity", 0, {
        time = 0.45,
        ease = "in_quad",
        done = function() zaro.destroy(ring) end,
    })
end

local function spawn_elite()
    -- A point on the arena's edge, from the seeded math.random.
    local t = 4 + math.random() * (ARENA - 8)
    local side = math.random(4)
    local x, z = t, 3
    if side == 2 then
        x, z = t, ARENA - 3
    elseif side == 3 then
        x, z = 3, t
    elseif side == 4 then
        x, z = ARENA - 3, t
    end
    elites[#elites + 1] = zaro.spawn("elite", { Transform = { position = { x = x, y = 0, z = z } } })
end

local function start_wave()
    wave += 1
    local speed = math.min(zaro.get("horde", "CrowdSpawner", "speed") + SPEEDUP, MAX_SPEED)
    zaro.set("horde", "CrowdSpawner", "speed", speed)
    for _ = 1, wave * 2 do
        spawn_elite()
    end
    if wave % 2 == 0 then
        add_blade()
    end
    zaro.emit("wave", { wave = wave, speed = speed })
end

function on_start()
    add_blade()
    add_blade()
    zaro.every(WAVE_SECONDS, start_wave)

    zaro.on("wave", function(w)
        zaro.log("wave", w.wave, "horde speed", w.speed, "score", score)
    end)
    zaro.on("crowd_hit", function()
        combo = 0
    end)
    zaro.on("elite_down", function(e)
        score += 25 + combo
        shockwave(e.x, e.z, "#ffd23d")
    end)
    zaro.on("player_hit", function()
        combo = 0
        zaro.set("player", "Shape", "flash", 1)
        zaro.set("camera", "ScreenFx", "shake", 1)
    end)
end

-- Elites walk straight at the player; a blade takes them down, the player takes the hit.
local function move_elites(player, tips)
    local kept = {}
    for _, elite in ipairs(elites) do
        local p = zaro.get(elite, "Transform", "position")
        local dx, dz = player.x - p.x, player.z - p.z
        local d = math.sqrt(dx * dx + dz * dz)
        local down = false
        for _, tip in ipairs(tips) do
            local tx, tz = tip.x - p.x, tip.z - p.z
            if tx * tx + tz * tz < ELITE_HIT * ELITE_HIT then
                down = true
            end
        end
        if down then
            zaro.destroy(elite)
            zaro.emit("elite_down", { x = p.x, z = p.z })
        elseif d < 1.0 then
            zaro.destroy(elite)
            zaro.emit("player_hit")
        else
            local step = math.min(ELITE_SPEED, d)
            zaro.set(elite, "Transform", "position", { x = p.x + dx / d * step, y = p.y, z = p.z + dz / d * step })
            kept[#kept + 1] = elite
        end
    end
    elites = kept
end

function on_tick(tick)
    zaro.set("rotor", "Transform", "rotation", (tick * ROTOR_DEGREES) % 360)
    local tips = {}
    for i, blade in ipairs(blades) do
        local tip = zaro.world(blade)
        tips[i] = tip
        local swept = zaro.crowd_kill(tip.x, tip.z, SWEEP_RADIUS)
        if swept > 0 then
            score += swept
            combo += swept
            zaro.set(blade, "ParticleEmitter", "burst", math.min(swept * 4, 40))
        end
    end
    move_elites(zaro.world("player"), tips)
end
