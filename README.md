# AudioUtilTTS

Live text-to-speech for Skyrim SE. It gives a voice to lines that have none:

- **Dialogue without voice files.** Plenty of mod dialogue ships as text only. The
  plugin speaks those lines while you play, in the voice set up for the
  speaker's voice type.
- **Lines a mod hands it.** A mod sends a line of text and it is spoken a moment
  later, as narration or by a character, through
  [AudioUtil](https://github.com/crajjjj/AudioUtil).

The voice model runs inside the plugin, on the CPU, with no internet connection
and no helper program. Speech is generated on background threads, not on the
game's own.

## Documentation

Everything is on the docs site: **https://crajjjj.github.io/AudioUtilTTS/**

| I want to | Page |
|---|---|
| Install it and hear it | [Getting Started](https://crajjjj.github.io/AudioUtilTTS/players/getting-started/) |
| Know what it costs and how it behaves | [What to Expect](https://crajjjj.github.io/AudioUtilTTS/players/what-to-expect/) |
| Have dialogue without voice files spoken | [Unvoiced Dialogue](https://crajjjj.github.io/AudioUtilTTS/players/unvoiced-dialogue/) |
| Decide which NPC gets which voice | [Voices, Presets & Voice Types](https://crajjjj.github.io/AudioUtilTTS/players/voices/) |
| Add voices | [Voice Models](https://crajjjj.github.io/AudioUtilTTS/players/models/) |
| Look up a setting or a console command | [Configuration Reference](https://crajjjj.github.io/AudioUtilTTS/players/configuration/), [Console Commands](https://crajjjj.github.io/AudioUtilTTS/players/console-commands/) |
| Find out why something is silent | [Troubleshooting & Logs](https://crajjjj.github.io/AudioUtilTTS/players/troubleshooting/) |
| Make my mod talk | [Overview](https://crajjjj.github.io/AudioUtilTTS/authors/overview/), [Mod Events](https://crajjjj.github.io/AudioUtilTTS/authors/modevents/), [Papyrus API](https://crajjjj.github.io/AudioUtilTTS/authors/papyrus-api/), [C++ API](https://crajjjj.github.io/AudioUtilTTS/authors/cpp-api/) |
| Make or release a voice | [Training a Voice](https://crajjjj.github.io/AudioUtilTTS/authors/training-voices/), [Shipping a Voice Model](https://crajjjj.github.io/AudioUtilTTS/authors/voice-models/) |
| Understand or build the plugin | [How It Works](https://crajjjj.github.io/AudioUtilTTS/authors/how-it-works/), [Tools](https://crajjjj.github.io/AudioUtilTTS/authors/tools/), [Building from Source](https://crajjjj.github.io/AudioUtilTTS/authors/building/) |

The changelog is on the [releases page](https://github.com/crajjjj/AudioUtilTTS/releases).

## Install in short

Requirements: Skyrim SE / AE with SKSE64 and Address Library,
[AudioUtil](https://github.com/crajjjj/AudioUtil) 0.9.29 or newer, and
[ConsoleUtil Extended](https://www.nexusmods.com/skyrimspecialedition/mods/133569)
for the `autts` console commands.

From the [latest release](https://github.com/crajjjj/AudioUtilTTS/releases/latest),
installed like any mod:

- `AudioUtilTTS-<version>.zip`: the plugin. The only archive that is required.
- `AudioUtilTTS-Voice-kokoro-en-v0_19.zip`: the default voice model, for
  narration and for lines that name no voice of their own.

A voice archive named after a voice type (`models\MaleNord`) speaks for the NPCs
of that voice type with nothing to configure. Without any voice the plugin loads
and stays silent; `autts status` in the console says why.

What each archive gives you, and a first check in the console:
[Getting Started](https://crajjjj.github.io/AudioUtilTTS/players/getting-started/).

## For mod authors in short

No dependency is needed to speak a line. Send a mod event:

```papyrus
; narration: flat, no speaker
SendModEvent("AudioUtilTTS_Speak::MyMod", "The road to Whiterun lay open.")

; a character says it: positioned at them, mouth moving
akSpeaker.SendModEvent("AudioUtilTTS_Say::MyMod", "Not so loud.")
```

Game dialogue needs no call at all: ship it without voice files. The event
names, the Papyrus natives and the C++ exports are described in the
[Overview](https://crajjjj.github.io/AudioUtilTTS/authors/overview/).

## Building

```
git submodule update --init --recursive
powershell -ExecutionPolicy Bypass -File scripts\fetch-deps.ps1
xmake f -m release
xmake                      # DLL -> dist\SKSE\Plugins\AudioUtilTTS.dll
xmake build papyrus        # scripts -> dist\Scripts
xmake build package        # release archives -> Release\
```

Needs VS 2022 (v143, C++23) and xmake 3.0 or newer. The details are in
[Building from Source](https://crajjjj.github.io/AudioUtilTTS/authors/building/).

## Licenses

AudioUtilTTS is GPLv3 (see `LICENSE`). It links
[CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) (GPLv3 or
later, with a modding and a linking exception),
[sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) (Apache 2.0),
[ONNX Runtime](https://github.com/microsoft/onnxruntime) (MIT) and, through
sherpa-onnx, [espeak-ng](https://github.com/espeak-ng/espeak-ng) (GPLv3). The
plugin archive carries the full notices and license texts in
`SKSE\Plugins\AudioUtilTTS\licenses`. Voice models carry their own license, in
the model folder.
