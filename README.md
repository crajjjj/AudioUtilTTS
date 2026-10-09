# AudioUtilTTS

Live text-to-speech for Skyrim SE. A mod hands it a line of text and the line is
spoken in game a moment later, through [AudioUtil](https://github.com/crajjjj/AudioUtil).
The voice model runs inside the plugin, on the CPU, with no internet connection
and no helper program.

## Requirements

- Skyrim SE / AE with SKSE64 and Address Library
- AudioUtil 0.9.9 or newer (it does the actual playback); 0.9.26 or newer for
  the mouth to move on character speech
- ConsoleUtil Extended, only for the `autts` test commands

## Installing

Take both archives from the [latest release](https://github.com/crajjjj/AudioUtilTTS/releases/latest)
and install them like any mod:

- `AudioUtilTTS-<version>.zip`: the plugin, its config, scripts and console
  commands.
- `AudioUtilTTS-Voice-kokoro-en-v0_19.zip`: the default voice model. It is a
  separate download because it is large and does not change with the plugin.

Without a voice model the plugin loads but stays silent; `autts status` says
why.

## Using it from a mod

The simplest way needs no dependency on this mod at all. Send a mod event from
any Form:

```papyrus
SendModEvent("AudioUtilTTS_Speak", "The road to Whiterun lay open.")
```

It is spoken with the configured default voice. Without AudioUtilTTS installed
the event goes nowhere. Use `SendModEvent` (the Form function), not
`ModEvent.Create`.

To choose who speaks, name a preset after a colon:

```papyrus
SendModEvent("AudioUtilTTS_Speak:whisper", "Not so loud.")
```

A preset is a voice the user defines in the config under one name (see
[Presets](#presets)). An unknown name falls back to the default voice, so a mod
can ask for a role without knowing what the user has installed.

Those are narration: flat, with no speaker. To make a character say a line, let
that actor send `AudioUtilTTS_Say`. The line is positioned at them and their
mouth moves:

```papyrus
akSpeaker.SendModEvent("AudioUtilTTS_Say", "Not so loud.")
akSpeaker.SendModEvent("AudioUtilTTS_Say:whisper", "Not so loud.")
```

With no preset named, the speaker gets the preset `female` or `male` by their sex
when the config defines those. See [Character speech](#character-speech).

To learn whether a line was accepted, or to pass a voice number, volume or speed
directly, call the natives and guard them:

```papyrus
If SKSE.GetPluginVersion("AudioUtilTTS") >= 0
    AudioUtilTTS.SpeakAs("Not so loud.", "whisper")
    AudioUtilTTS.Speak("The road to Whiterun lay open.", 9)
EndIf
```

When a line starts playing, the mod event `AudioUtilTTS_Spoken` fires with
`strArg` = the text and `numArg` = the AudioUtil instance handle. The full API is
documented in `papyrus\Source\AudioUtilTTS.psc`.

## What to expect

- **Speech starts late.** A line is generated when it is asked for. With the
  default model a sentence takes about two seconds on two threads of an
  i7-12700KF with the game closed, and longer while the game is busy.
- **Lines do not pile up.** A new line waits for the one being spoken; if more
  arrive meanwhile, only the newest is kept (`[playback] max_pending`).
- **Narration and character speech are separate.** Narration plays flat and moves
  no mouth. A character line plays at the actor and moves their mouth. Neither
  goes through the game's dialogue system, so there are no dialogue subtitles.

## Character speech

`AudioUtilTTS.SpeakActor(akSpeaker, "text")`, or the actor sending
`AudioUtilTTS_Say`, makes that actor say the line.

- The mouth follows the loudness of the line (AudioUtil's lipsync). It opens and
  closes with the speech; it does not form real phonemes. Needs AudioUtil
  0.9.26 or newer, otherwise the mouth stays still.
- AudioUtil's guards still apply: a gag, a worn tongue item, a dialogue with the
  player or another mod's claim on the mouth all keep it still.
- Character lines use their own channel and placeholder files (`[speech]` in the
  config), so a character and the narrator can talk at once. Two characters
  take turns.
- A generated voice will not match an NPC's recorded voice.
- If the speaker is gone or unloaded by the time the line is ready, it is
  dropped.

## Cost

Measured outside the game on an i7-12700KF, for lines of about five seconds of
speech. Nothing here uses the GPU. In a running game on that machine no drop in
frame rate was noticed while lines were generated (observed, not benchmarked).

| | Kokoro (default) | Piper voice |
|---|---|---|
| Memory, settled after a few lines | about 625 MB | about 350 MB |
| Time to generate a line, 2 threads | about 2 s | about 0.25 s |
| CPU while generating | 2 cores fully busy | 2 cores fully busy |
| CPU between lines | none | none |
| Time to load, on a background thread | 0.8 s | 0.6 s |

- A line costs CPU only while it is generated, plus a short wind-down right
  after (about a third of a CPU-second with two threads). `[model] threads` trades burst
  width for length: one thread keeps a single core busy for about 3.2 s per
  line, four threads keep four busy for about 1.2 s.
- Memory does not grow with use: it was flat from the fifth line to the
  thirtieth.
- Presets on the default model cost nothing extra. A preset on another model
  adds that model's memory once it has been used.

## Console commands

| Command | Does |
|---|---|
| `autts say "text"` | Speak a line with the configured voice |
| `autts sayv "text" 9` | Speak a line with voice number 9 of the default model |
| `autts sayas "text" whisper` | Speak a line through the preset `whisper` |
| `autts sayactor "text"` | Make the console-selected actor (or the player) say a line |
| `autts sayactoras "text" whisper` | The same through the preset `whisper` |
| `autts sayevent "text"` | The same through the `AudioUtilTTS_Say` mod event |
| `autts event "text"` | Speak a line through the mod event |
| `autts eventas "text" whisper` | Speak a line through the mod event with a preset |
| `autts status` | Show whether speech is ready, and why not |
| `autts stop` | Drop waiting lines and cut the current one |
| `autts reload` | Re-read the config and reload the model |
| `autts enable 0` | Switch speech off for this session (`1` = on) |

## Configuration

`Data\SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml` holds the default model and
voice, the presets, thread count and playback behaviour. Every key is commented
in the file. Volume also follows AudioUtil's `narration` group.

### Presets

A preset gives a model, a voice number, a speed and a volume one name that mods
ask for:

```toml
[preset.whisper]
voice = 2

[preset.fast]
model = "vits-piper-en_US-lessac-medium"
speed = 1.1
```

Anything a preset leaves out comes from the defaults. The shipped config defines
`narrator`, `female`, `male` and `whisper`, all on the default model.

A preset may name a different model. That model is loaded the first time a line
asks for it, which delays that one line by about a second, and then stays in
memory (see [Cost](#cost)). `[model] max_loaded` (default 3) caps how many are
kept; the least recently used one is unloaded first, never the default.

### Models

Models go into `Data\SKSE\Plugins\AudioUtilTTS\models\<folder>`. Kokoro
(`kokoro-en-v0_19`, the default) and VITS models such as the Piper voices from
the [sherpa-onnx model list](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models)
are supported. `kokoro-en-v0_19` and `vits-piper-en_US-lessac-medium` have been
checked outside the game, loaded together.

A model that the runtime cannot read can close the game while loading, because
the runtime exits on some malformed files. Check a new model outside the game
first with the probe (see below).

## How it works

Skyrim only plays loose files it saw at launch, but it re-reads a file's bytes
every time it plays one. The mod ships a few silent placeholder wavs
(`Data\Sound\AudioUtilTTS\_tts_slotNN.wav`); the plugin writes each generated
line into the next one and asks AudioUtil to play that file. AudioUtil needs no
change for this.

The speech runtime (sherpa-onnx with ONNX Runtime) is linked statically, so the
mod is a single DLL. That is deliberate: a separate `onnxruntime.dll` would
share its module name with the copy other mods load.

## Building

```
git submodule update --init --recursive
powershell -ExecutionPolicy Bypass -File scripts\fetch-deps.ps1
xmake f -m release
xmake                      # DLL -> dist\SKSE\Plugins\AudioUtilTTS.dll
xmake build papyrus        # scripts -> dist\Scripts
xmake build package        # release archives -> Release\
```

- Needs VS 2022 (v143, C++23) and xmake 3.0 or newer.
- `fetch-deps.ps1` downloads the prebuilt runtime libraries (about 1 GB
  unpacked, into `extern\`) and the default voice model (about 350 MB, into
  `dist\`). Neither is tracked in git.
- `AudioUtilTTS.ppj` holds machine-local paths and is not tracked either.
- Both build modes link the release static CRT, because the prebuilt runtime
  libraries do.

`xmake build probe` builds a small console program that runs the same speech
core outside the game:

```
xmake run probe dist\SKSE\Plugins\AudioUtilTTS\models\kokoro-en-v0_19 "Some text" 7 out.wav
```

## Licenses

The plugin archive carries the full notices and license texts in
`SKSE\Plugins\AudioUtilTTS\licenses`.

AudioUtilTTS is GPLv3 (see `LICENSE`). It links
[sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) (Apache 2.0),
[ONNX Runtime](https://github.com/microsoft/onnxruntime) (MIT) and, through
sherpa-onnx, [espeak-ng](https://github.com/espeak-ng/espeak-ng) (GPLv3). Voice
models carry their own license, in the model folder.
