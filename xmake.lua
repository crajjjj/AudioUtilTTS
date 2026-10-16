set_xmakever("3.0.0")  -- the commonlibsse-ng submodule requires 3.0

-- Globals
PROJECT_NAME = "AudioUtilTTS"
PROJECT_VERSION = "0.4.1"
PROJECT_AUTHOR = "crajjjj"

-- Project
set_project(PROJECT_NAME)
set_version(PROJECT_VERSION)
set_languages("cxx23")
set_license("gplv3")
set_warnings("allextra")

-- Options
option("copy_to_mod")
    set_default(false)
    set_description("Copy dist/* to a mod folder (XSE_TES5_MODS_PATH)")
option_end()

-- Dependencies & Includes
includes("lib/commonlibsse-ng")

add_requires("toml++")

-- policies
set_policy("package.requires_lock", true)

-- rules
add_rules("mode.debug", "mode.release")

-- The speech runtime in extern\sherpa-onnx is a set of prebuilt static libraries
-- compiled against the release static CRT, so BOTH modes link "MT": a debug
-- build is an unoptimized release-CRT build, never "MTd" (that fails to link).
set_runtimes("MT")
if is_mode("debug") then
    add_defines("DEBUG")
    set_optimize("none")
elseif is_mode("release") then
    add_defines("NDEBUG")
    set_optimize("fastest")
    set_symbols("debug")
end

add_defines("_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING")
add_defines("NOMINMAX")  -- CommonLib v7 headers pull in Windows.h

-- sherpa-onnx + onnxruntime, linked statically on purpose: a separate
-- onnxruntime.dll would share its module name with the copy other mods load,
-- and Windows resolves an already-loaded module name to whichever came first.
-- Fetch with scripts\fetch-deps.ps1.
local SHERPA_DIR = path.join(os.scriptdir(), "extern", "sherpa-onnx")

local function use_sherpa()
    add_includedirs(path.join(SHERPA_DIR, "include"))
    add_linkdirs(path.join(SHERPA_DIR, "lib"))
    for _, lib in ipairs(os.files(path.join(SHERPA_DIR, "lib", "*.lib"))) do
        add_links(path.basename(lib))
    end
end

-- Target
target(PROJECT_NAME)
    set_kind("shared")

    -- CommonLibSSE-NG
    add_deps("commonlibsse-ng")
    add_rules("commonlibsse-ng.plugin", {
        name = PROJECT_NAME,
        author = PROJECT_AUTHOR,
        description = "Live text-to-speech for AudioUtil: speaks any text a mod hands it, and dialogue that has no recorded voice."
    })

    -- Packages
    add_packages("toml++")
    use_sherpa()

    -- Source files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    -- vendor\SKSEMenuFramework: the in-game menu's client header (src\menu\Ui.h)
    add_includedirs("src", "include", "vendor/SKSEMenuFramework")
    set_pcxxheader("src/PCH.h")

    -- flags
    add_cxxflags(
        "cl::/diagnostics:caret",
        "cl::/wd4200",
        "cl::/wd4201",
        "cl::/Zc:preprocessor",
        "cl::/utf-8"
    )

    if is_mode("debug") then
        add_cxxflags("cl::/bigobj")
    end

    -- Post Build
    after_build(function (target)
        local plugin_folder = path.join(os.projectdir(), "dist", "SKSE", "Plugins")
        if not os.isdir(plugin_folder) then
            os.mkdir(plugin_folder)
        end
        os.cp(target:targetfile(), plugin_folder)
        if is_mode("debug") then
            local pdb = target:symbolfile()
            if pdb then
                os.cp(pdb, plugin_folder)
            end
        end
        -- placeholder wavs the plugin speaks into; they ship with the mod so the
        -- game indexes them at the first launch (see scripts\slots.lua)
        import("scripts.slots")
        slots(path.join(os.projectdir(), "dist", "Sound", "AudioUtilTTS"), 4, "_tts_slot")
        slots(path.join(os.projectdir(), "dist", "Sound", "AudioUtilTTS"), 4, "_tts_speech")
        -- dialogue lines are played by the game as voice files: wav + lip pairs
        slots(path.join(os.projectdir(), "dist", "Sound", "Voice", "AudioUtilTTS"), 128, "_tts_dialog", true)
        -- the in-game menu's defaults and its "reset": the config as shipped, which
        -- the user's own copy no longer is once they edit it
        local config_dir = path.join(os.projectdir(), "dist", "SKSE", "Plugins", "AudioUtilTTS")
        os.cp(path.join(config_dir, "AudioUtilTTS.toml"), path.join(config_dir, "AudioUtilTTS.defaults.toml"))

        local mod_folder = os.getenv("XSE_TES5_MODS_PATH")
        if mod_folder and has_config("copy_to_mod") then
            os.cp("dist/*", path.join(mod_folder, "AudioUtilTTS"))
        end
    end)
target_end()

-- Offline check of the speech core, no game needed: xmake build probe
-- Then:  xmake run probe <model dir> "text" [voice] [out.wav] [threads]
target("probe")
    set_kind("binary")
    set_default(false)
    add_files("tools/probe/main.cpp", "src/TtsEngine.cpp", "src/Lip.cpp")
    add_includedirs("src")
    set_rundir("$(projectdir)")  -- `xmake run probe` takes paths relative to the repo
    use_sherpa()
    add_cxxflags("cl::/utf-8")
target_end()

-- The in-game menu's config writer and schema, no game needed:
--   xmake build menu-tests && xmake run menu-tests
-- Runs against the shipped AudioUtilTTS.toml (see tests\menu_tests.cpp).
target("menu-tests")
    set_kind("binary")
    set_default(false)
    add_packages("toml++")
    add_files("src/menu/ConfigDoc.cpp", "src/menu/Schema.cpp", "src/menu/Model.cpp", "src/menu/MenuSchema.cpp", "tests/menu_tests.cpp")
    add_includedirs("src")
    set_rundir("$(projectdir)")
    add_cxxflags("cl::/utf-8")
target_end()

-- Release archives: xmake build package
-- Zips dist\ into Release\ as the plugin archive plus one archive per voice
-- model (see scripts\package.lua). Build the plugin and the scripts first.
target("package")
    set_kind("phony")
    set_default(false)
    on_build(function (target)
        import("scripts.package")
        package()
    end)
target_end()

-- Voice training kit: xmake build voicetrain
-- Zips tools\voicetrain into Release\AudioUtilTTS-VoiceTraining-<version>.zip,
-- a download of its own for people who make voices (see scripts\package.lua).
target("voicetrain")
    set_kind("phony")
    set_default(false)
    on_build(function (target)
        import("scripts.package")
        package.voicetrain()
    end)
target_end()

-- Papyrus compile: xmake build papyrus
-- Runs Pyro on AudioUtilTTS.ppj (see scripts\pyro.lua; overrides: PYRO_EXE,
-- SKYRIM_GAME_PATH)
target("papyrus")
    set_kind("phony")
    set_default(false)
    on_build(function (target)
        import("scripts.pyro")
        pyro()
    end)
target_end()
