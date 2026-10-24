-- Feeds Bri0/Bri1/Bri2 from bri_state.txt (written by brightness_helper.py),
-- and forwards set/bump requests to the daemon via bri_cmd.txt.
-- Set/Bump also apply an optimistic local update so the slider moves within
-- one frame; the daemon writes DDC afterwards and its reading realigns us.
function Initialize()
    statePath = SKIN:GetVariable("@") .. "bri_state.txt"
    cmdPath = SKIN:GetVariable("@") .. "bri_cmd.txt"
    -- Launch the helper daemon (idempotent via its pid file). Runs on every
    -- skin load/refresh, so it follows Rainmeter starting up.
    -- Escaped double-quoted string (backslashes doubled for Lua).
    SKIN:Bang("[\"C:\\Users\\steven\\AppData\\Local\\Programs\\Python\\Python312\\pythonw.exe\" \"C:\\Users\\steven\\Documents\\Rainmeter\\Skins\\QuantoFlx\\Package\\QuantoFlx.System\\ScreenBrightness\\brightness_helper.py\" \"daemon\"]")
end

function Update()
    local f = io.open(statePath, "r")
    if not f then
        return -1
    end
    local line = f:read("*l")
    f:close()
    if not line then
        return -1
    end
    local first = -1
    local i = 0
    for v in line:gmatch("%S+") do
        SKIN:Bang("!SetVariable", "Bri" .. i, v)
        if i == 0 then
            first = tonumber(v) or -1
        end
        i = i + 1
    end
    return first
end

-- !CommandMeasure Ms.BriFeed "Set(0, 70)"
function Set(idx, val)
    local v = clamp(tonumber(val))
    if v < 0 then
        return
    end
    writeCmd("set " .. idx .. " " .. v)
    applyLocal(idx, v)
end

-- !CommandMeasure Ms.BriFeed "Bump(0, 2)"
function Bump(idx, delta)
    local d = tonumber(delta) or 0
    local cur = tonumber(SKIN:GetVariable("Bri" .. idx)) or -1
    if cur < 0 then
        return
    end
    writeCmd("bump " .. idx .. " " .. d)
    applyLocal(idx, clamp(cur + d))
end

function clamp(v)
    if v == nil or v < 0 then
        return -1
    end
    if v > 100 then
        return 100
    end
    return math.floor(v + 0.5)
end

function applyLocal(idx, v)
    SKIN:Bang("!SetVariable", "Bri" .. idx, v)
    SKIN:Bang("!UpdateMeter", "*")
    SKIN:Bang("!Redraw")
end

function writeCmd(payload)
    local f = io.open(cmdPath, "a")
    if f then
        f:write(payload)
        f:write(string.char(10))
        f:close()
    end
end
