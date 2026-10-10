-- Writes the placeholder files the plugin speaks into:
--   dist\Sound\AudioUtilTTS\_tts_slotNN.wav          narration
--   dist\Sound\AudioUtilTTS\_tts_speechNN.wav        character speech
--   dist\Sound\Voice\AudioUtilTTS\_tts_dialogNN.wav  dialogue, each with a .lip
-- Skyrim only plays loose files it indexed at launch, but it re-reads a file's
-- bytes on every play, so the plugin overwrites one of these per spoken line
-- (Slots in src\Speaker.cpp). Shipping them means they are indexed at a fresh
-- install's first launch; the plugin can create missing ones itself, but those
-- only start working after a restart.
-- Each wav is a tiny valid mono 44.1 kHz 16-bit silent file. Each lip is a
-- closed mouth, the same bytes as Lip::Rest() in src\Lip.cpp. Names must match
-- SLOT_PREFIX and SLOT_DIR_REL in src\Speaker.cpp; the counts mirror the
-- [playback] slots, [speech] slots and [dialogue] slots defaults.
function main(dir, count, prefix, with_lip)
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
    local function write(file, bytes)
        if not os.isfile(file) then
            local f = io.open(file, "wb")
            if f then
                f:write(bytes)
                f:close()
            end
        end
    end
    local dataBytes = 2000
    local wav = "RIFF" .. le(36 + dataBytes, 4) .. "WAVE"
        .. "fmt " .. le(16, 4) .. le(1, 2) .. le(1, 2)
        .. le(44100, 4) .. le(88200, 4) .. le(2, 2) .. le(16, 2)
        .. "data" .. le(dataBytes, 4) .. string.rep("\0", dataBytes)
    -- version 1, size 132 * 2 frames + 28, 13, then the zero-run stream of:
    -- 2 frames, preroll -1, 16 tracks and a grid of 2 x 33 weights, all at rest
    local lip = le(1, 4) .. le(292, 4) .. le(13, 4)
        .. string.char(2, 0, 3, 0, 255, 255, 255, 255, 16, 0, 11, 1)
    for i = 0, count - 1 do
        local stem = path.join(dir, string.format("%s%02d", prefix, i))
        write(stem .. ".wav", wav)
        if with_lip then
            write(stem .. ".lip", lip)
        end
    end
end
