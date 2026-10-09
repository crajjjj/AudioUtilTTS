-- Writes the placeholder wavs the plugin speaks into: dist\Sound\AudioUtilTTS\
-- _tts_slotNN.wav. Skyrim only plays loose files it indexed at launch, but it
-- re-reads a file's bytes on every play, so the plugin overwrites one of these
-- per spoken line (Slots in src\Speaker.cpp). Shipping them means they are
-- indexed at a fresh install's first launch; the plugin can create missing ones
-- itself, but those only start working after a restart.
-- Each is a tiny valid mono 44.1 kHz 16-bit silent wav. Names must match
-- SlotName in src\Speaker.cpp; the count mirrors the [playback] slots default.
function main(dir, count)
    if not os.isdir(dir) then
        os.mkdir(dir)
    end
    local function le(v, n)
        local s = ""
        for _ = 1, n do
            s = s .. string.char(v % 256)
            v = math.floor(v / 256)
        end
        return s
    end
    local dataBytes = 2000
    local wav = "RIFF" .. le(36 + dataBytes, 4) .. "WAVE"
        .. "fmt " .. le(16, 4) .. le(1, 2) .. le(1, 2)
        .. le(44100, 4) .. le(88200, 4) .. le(2, 2) .. le(16, 2)
        .. "data" .. le(dataBytes, 4) .. string.rep("\0", dataBytes)
    for i = 0, count - 1 do
        local file = path.join(dir, string.format("_tts_slot%02d.wav", i))
        if not os.isfile(file) then
            local f = io.open(file, "wb")
            if f then
                f:write(wav)
                f:close()
            end
        end
    end
end
