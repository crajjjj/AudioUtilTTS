# Building from Source

AudioUtilTTS is a C++ SKSE plugin (CommonLibSSE-NG, built with xmake) plus two Papyrus scripts. The speech runtime is a set of prebuilt static libraries fetched by a script.

## Requirements

- Visual Studio 2022 (MSVC v143, C++23)
- xmake 3.0 or newer
- CommonLibSSE-NG, a git submodule at `lib/commonlibsse-ng`
- For the scripts: Pyro, and AudioUtil's Papyrus sources

## Build

```
git submodule update --init --recursive
powershell -ExecutionPolicy Bypass -File scripts\fetch-deps.ps1
xmake f -m release
xmake                      # DLL -> dist\SKSE\Plugins\AudioUtilTTS.dll
xmake build papyrus        # scripts -> dist\Scripts
xmake build package        # release archives -> Release\
```

- **`fetch-deps.ps1`** downloads the prebuilt runtime libraries (about 1 GB unpacked, into `extern\`) and the default voice model (about 350 MB, into `dist\`). Neither is tracked in git. It is safe to re-run: anything already present is skipped. `-SkipModel` leaves the model out.
- **`AudioUtilTTS.ppj`** holds machine-local paths (the game, AudioUtil's sources) and is not tracked either.
- **Both build modes link the release static CRT**, because the prebuilt runtime libraries do. A debug build is an unoptimized build against the release CRT.
- **The build also writes the placeholder files** into `dist\Sound`, so they ship with the mod and the game indexes them at the first launch, and `AudioUtilTTS.defaults.toml`, a copy of the shipped config for the in-game menu's defaults and reset.

## Targets

| Command | Builds |
|---|---|
| `xmake` | The plugin DLL, copied to `dist\SKSE\Plugins\` (with the PDB in debug) |
| `xmake build papyrus` | The scripts, through Pyro |
| `xmake build package` | `Release\AudioUtilTTS-<version>.zip` and one `AudioUtilTTS-Voice-<model>.zip` per model folder in `dist`. Build the plugin and the scripts first |
| `xmake build probe` | The offline probe; see [Tools](tools.md#the-probe) |
| `xmake build voicetrain` | `Release\AudioUtilTTS-VoiceTraining-<version>.zip`: the [voice training kit](tools.md#the-voice-training-kit) as a download of its own |
| `xmake build menu-tests` | Checks for the in-game menu, run with `xmake run menu-tests` from the repo root: the schema describes every key of the shipped config, a change touches one line, a hand edit survives a write, and every framework function the menu calls is probed at load |
| `xmake build xtts-tests` | Checks for the endpoint SkyrimNet talks to, run with `xmake run xtts-tests`: an HTTP client sends what SkyrimNet sends (a line, a ping, a voice sample) to the server on a spare port, and a port another server holds is refused |

`xmake f --copy_to_mod=y` with the environment variable `XSE_TES5_MODS_PATH` set also copies `dist\*` into `<mods>\AudioUtilTTS` after each build.

| Variable | Purpose |
|---|---|
| `PYRO_EXE` | Path to `pyro.exe`, when it is not found by itself |
| `SKYRIM_GAME_PATH` | The game folder, for the Papyrus compile |
| `XSE_TES5_MODS_PATH` | Target of `--copy_to_mod=y` |

## Repo layout

```
src/                              C++ SKSE plugin (CommonLibSSE-NG)
  menu/                           the in-game menu: the config writer (from SLO VE's menu), the schema, the pages
vendor/SKSEMenuFramework/         SKSE Menu Framework's client header, vendored verbatim (hash in its README)
tests/                            menu_tests.cpp, text_tests.cpp, xtts_tests.cpp: offline checks
include/API/AudioUtilAPI.h        AudioUtil's C API header (resolved at run time)
papyrus/Source/                   Papyrus sources (.psc)
dist/                             the mod as installed; maps onto Data\
  SKSE/Plugins/AudioUtilTTS/
    AudioUtilTTS.toml             the config (tracked)
    licenses/                     third-party notices (tracked)
    models/                       voice models (fetched, not tracked)
  SKSE/CustomConsole/             the autts console command map (tracked)
scripts/                          fetch-deps.ps1, and the xmake helpers (package, pyro, slots)
tools/probe/                      the offline probe
tools/voicetrain/                 the voice training kit (runs under Linux: LF line endings)
tools/dbvo/                       the voice pack gap filler
docs/                             these pages (MkDocs Material)
lib/commonlibsse-ng/              SKSE framework (submodule)
extern/                           fetched runtime libraries (not tracked)
```

Everything else under `dist/` is generated: the DLL by xmake, the scripts by Pyro, the placeholder files by `scripts\slots.lua`, the model by `fetch-deps.ps1`.

## These docs

The site is built with [MkDocs Material](https://squidfunk.github.io/mkdocs-material/) from `docs/` and `mkdocs.yml`, and deployed to GitHub Pages by `.github/workflows/docs.yml` on every push to `main` that touches them.

To preview locally:

```
pip install mkdocs-material
mkdocs serve
```

The workflow builds with `mkdocs build --strict`, so a broken link fails the build.

## Licenses

AudioUtilTTS is GPLv3 (see `LICENSE`). It links [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) (GPLv3 or later, with a modding and a linking exception, and through it [HDE64](https://github.com/TsudaKageyu/minhook), BSD 2-Clause), [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) (Apache 2.0), [ONNX Runtime](https://github.com/microsoft/onnxruntime) (MIT), [cpp-httplib](https://github.com/yhirose/cpp-httplib) (MIT), [JSON for Modern C++](https://github.com/nlohmann/json) (MIT) and, through sherpa-onnx, [espeak-ng](https://github.com/espeak-ng/espeak-ng) (GPLv3). The plugin archive carries the full notices and license texts in `SKSE\Plugins\AudioUtilTTS\licenses`. Voice models carry their own license, in the model folder.
