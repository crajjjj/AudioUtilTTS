-- Runs Pyro on AudioUtilTTS.ppj: compiles papyrus\Source -> dist\Scripts and
-- mirrors the .psc sources into dist\Scripts\Source so the mod carries them.
-- Overrides: PYRO_EXE (pyro.exe path), SKYRIM_GAME_PATH (game root).
function main()
    local pyro = os.getenv("PYRO_EXE")
    if not pyro or not os.isfile(pyro) then
        local home = os.getenv("USERPROFILE") or ""
        local candidates = os.files(path.join(home,
            ".vscode", "extensions", "joelday.papyrus-lang-vscode-*", "pyro", "pyro.exe"))
        pyro = candidates and candidates[1] or nil
    end
    assert(pyro and os.isfile(pyro),
        "pyro.exe not found - set the PYRO_EXE environment variable")

    local game = os.getenv("SKYRIM_GAME_PATH")
        or "C:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition"

    local src_out = path.join(os.projectdir(), "dist", "Scripts", "Source")
    if not os.isdir(src_out) then
        os.mkdir(src_out)
    end
    os.cp(path.join(os.projectdir(), "papyrus", "Source", "*.psc"), src_out)

    os.execv(pyro, {
        "-i", path.join(os.projectdir(), "AudioUtilTTS.ppj"),
        "--game-path", game
    })
end
