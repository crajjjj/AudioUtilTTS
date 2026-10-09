# AudioUtilTTS

Live text-to-speech for Skyrim SE. A mod hands it a line of text and the line is
spoken in game a moment later, through [AudioUtil](https://github.com/crajjjj/AudioUtil).
The voice model runs inside the plugin, on the CPU, with no internet connection
and no helper program.

## Requirements

- Skyrim SE / AE with SKSE64 and Address Library
- AudioUtil 0.9.9 or newer (it does the actual playback)
- ConsoleUtil Extended, only for the `autts` test commands

## Using it from a mod

The simplest way needs no dependency on this mod at all. Send a mod event from
any Form:

```papyrus
SendModEvent("AudioUtilTTS_Speak", "The road to Whiterun lay open.")
```

It is spoken with the configured voice. Without AudioUtilTTS installed the event
goes nowhere. Use `SendModEvent` (the Form function), not `ModEvent.Create`.

For a voice, volume or speed per line, call the native and guard it:

```papyrus
If SKSE.GetPluginVersion("AudioUtilTTS") >= 0
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
- **It is narration, not dialogue.** Speech plays flat, not positioned at an
  actor, and moves no mouth.

## Console commands

| Command | Does |
|---|---|
| `autts say "text"` | Speak a line with the configured voice |
| `autts sayv "text" 9` | Speak a line with voice number 9 |
| `autts event "text"` | Speak a line through the mod event |
| `autts status` | Show whether speech is ready, and why not |
| `autts stop` | Drop waiting lines and cut the current one |
| `autts reload` | Re-read the config and reload the model |
| `autts enable 0` | Switch speech off for this session (`1` = on) |

## Configuration

`Data\SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml` holds the model folder, the
voice number, speed, volume, thread count and playback behaviour. Every key is
commented in the file. Volume also follows AudioUtil's `narration` group.

Other models go into `Data\SKSE\Plugins\AudioUtilTTS\models\<folder>`. Kokoro
(`kokoro-en-v0_19`, the default) and VITS models such as the Piper voices from
the [sherpa-onnx model list](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models)
are supported. Only the default model has been run in game.

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

AudioUtilTTS is GPLv3 (see `LICENSE`). It links
[sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) (Apache 2.0),
[ONNX Runtime](https://github.com/microsoft/onnxruntime) (MIT) and, through
sherpa-onnx, [espeak-ng](https://github.com/espeak-ng/espeak-ng) (GPLv3). Voice
models carry their own license, in the model folder.
