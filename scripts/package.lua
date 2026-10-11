-- Builds the release archives from dist\ into Release\:
--   AudioUtilTTS-<version>.zip            the plugin: DLL, config, scripts,
--                                         console commands, placeholder wavs
--   AudioUtilTTS-Voice-<model>.zip        one per model folder in dist
-- Both unpack straight into Data\. The model is a separate archive on purpose:
-- it is hundreds of MB and does not change with the plugin, so an update is a
-- small download.
-- Uses tar.exe (ships with Windows 10 and later), which writes zip by suffix.
function main()
    import("core.project.project")
    local proj = os.projectdir()
    local dist = path.join(proj, "dist")
    local rel = path.join(proj, "Release")
    local version = project.version()
    assert(version and version ~= "", "package: no project version")
    assert(os.isfile(path.join(dist, "SKSE", "Plugins", "AudioUtilTTS.dll")),
        "package: build the plugin first (xmake)")
    assert(os.isfile(path.join(dist, "Scripts", "AudioUtilTTS.pex")),
        "package: compile the scripts first (xmake build papyrus)")
    if not os.isdir(rel) then
        os.mkdir(rel)
    end

    -- the plugin's own license travels with the binary, beside the others
    os.cp(path.join(proj, "LICENSE"),
        path.join(dist, "SKSE", "Plugins", "AudioUtilTTS", "licenses", "GPL-3.0.txt"))

    local models_rel = "SKSE/Plugins/AudioUtilTTS/models"

    local plugin_zip = path.join(rel, "AudioUtilTTS-" .. version .. ".zip")
    os.rm(plugin_zip)
    os.execv("tar", { "-a", "-c", "-f", plugin_zip, "-C", dist,
        "--exclude", models_rel, "--exclude", "*.pdb",
        "Scripts", "SKSE", "Sound" })
    print("package: " .. plugin_zip)

    for _, dir in ipairs(os.dirs(path.join(dist, "SKSE", "Plugins", "AudioUtilTTS", "models", "*"))) do
        local name = path.filename(dir)
        local model_zip = path.join(rel, "AudioUtilTTS-Voice-" .. name .. ".zip")
        os.rm(model_zip)
        os.execv("tar", { "-a", "-c", "-f", model_zip, "-C", dist, models_rel .. "/" .. name })
        print("package: " .. model_zip)
    end
end

-- Release\AudioUtilTTS-VoiceTraining-<version>.zip: tools\voicetrain as it is.
-- The scripts in it run under Linux, so they keep their LF line endings
-- (.gitattributes), and tar stores the bytes untouched.
function voicetrain()
    import("core.project.project")
    local proj = os.projectdir()
    local rel = path.join(proj, "Release")
    local version = project.version()
    assert(version and version ~= "", "voicetrain: no project version")
    if not os.isdir(rel) then
        os.mkdir(rel)
    end
    local zip = path.join(rel, "AudioUtilTTS-VoiceTraining-" .. version .. ".zip")
    os.rm(zip)
    os.execv("tar", { "-a", "-c", "-f", zip, "-C", path.join(proj, "tools"),
        "--exclude", "__pycache__", "voicetrain" })
    print("package: " .. zip)
end
