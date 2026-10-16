# Voice Models

A model is a folder of files the speech runtime reads, under `Data\SKSE\Plugins\AudioUtilTTS\models\<folder>`. The default one, `kokoro-en-v0_19`, is the second release archive. A voice archive is one more such folder, ready to install; the archives for the vanilla voice types are shared outside this site, on LoversLab.

## Adding a model

**A folder named like a voice type needs nothing else.** `models\MaleNord` speaks for the voice type `MaleNord` as soon as it is installed. That is why a voice archive named after a voice type holds no `model.toml`.

A folder with any other name does nothing until something names it:

- `[model] dir = "<folder>"` makes it the default model;
- a [preset](voices.md#presets) with `model = "<folder>"`;
- its own [`model.toml`](#models-that-bring-their-own-voice-types), a [voice type patch](voices.md#voice-type-patches) or a [`[voicetype]`](voices.md#voice-types) line.

Then run `autts reload`. After replacing a model's files, `autts reload` too: dialogue lines are cached per model file, so the old model's lines are not played again.

!!! warning "Check a new model outside the game first"
    The speech runtime exits on some malformed model files, and takes the game with it. Try a new model with the probe first; see [Tools](../authors/tools.md#the-probe).

## The default voice is optional

Without the default model installed:

- **every voice with a model of its own speaks as usual**: dialogue and character lines of the voice types you have voices for;
- **lines that name no other model are not spoken**: narration, the shipped presets (`narrator`, `female`, `male`, `whisper`) and the generic voices of `[dialogue] fallback`;
- **`autts status` starts with `ready | no default model`** and says why;
- **its memory, about 0.6 GB, is free** for your voices.

## Which models work

- **Kokoro**, like the default model: one model, many voices.
- **VITS**, such as the **Piper** voices on the [sherpa-onnx model list](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models), also in other languages. Most have one voice, and they are several times faster than Kokoro; see [Cost](what-to-expect.md#cost).
- **Pocket**, Kyutai's [Pocket TTS](https://github.com/kyutai-labs/pocket-tts): one model that speaks like any short recording you give it. See [Pocket voices](#pocket-voices).

The multi-language Kokoro builds on the same list have not been tried with this plugin.

To make a voice of your own, see [Training a Voice](../authors/training-voices.md), or record one for [Pocket](#pocket-voices).

## Pocket voices

A Pocket model has no voices built in. It speaks like a recording: each wav file in its `voices` folder is one voice.

```
models\pocket-tts\
  lm_main.int8.onnx  lm_flow.int8.onnx  encoder.onnx  decoder.int8.onnx  text_conditioner.onnx
  vocab.json  token_scores.json
  voices\
    merchant.wav         the voice "merchant"
    old-woman.wav        the voice "old-woman"
    whisper.wav          the voice "whisper"
```

- **Get the model** as `AudioUtilTTS-Voice-pocket-tts.zip` from the [latest release](https://github.com/crajjjj/AudioUtilTTS/releases/latest), which installs it as `models\pocket-tts`, the folder the [fill-in voices](voices.md#fill-in-voices) archive and the examples here use. It is the `sherpa-onnx-pocket-tts-int8-2026-01-26` model from the [sherpa-onnx model list](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models); unpacked by hand, the folder name is yours to pick, but recordings and presets made for `pocket-tts` then need that name.
- **A recording is named by its file name**, without `.wav`: `voice = "old-woman"`. A number works too (the recording's place in name order, from 0), but a name stays right when recordings are added. `AudioUtilTTS.log` lists them when the model loads.
- **Anyone can add recordings.** A mod that needs a voice ships only its `voices\mymod-anna.wav` into the same folder (a mod manager merges the folders) and a preset or patch that names it. The model is loaded once for all of them, and a recording added or replaced leaves the cached lines of the others as they are.
- **A good recording** is 5 to 10 seconds of one person speaking, clean, with no music and no other voices. Only the first 10 seconds are used. Any wav works: 8 to 32-bit or float, mono or stereo, any sample rate.
- **The manner of the recording carries over.** A whispered recording gives whispered lines. So one speaker recorded in several moods can be several presets.
- **Speed has no effect on Pocket voices.** A slower or faster voice is a slower or faster recording.
- **Use only voices you may use.** Kyutai's terms for Pocket TTS forbid copying a person's voice without their consent. Kyutai publishes voices recorded for this, each with its license, at [kyutai/tts-voices](https://huggingface.co/kyutai/tts-voices).

A Pocket voice is used like any other, by the recording's name:

```toml
[preset.innkeeper]
model = "pocket-tts"
voice = "merchant"
```

- **One folder, many voices.** The model is loaded once, about 0.6 GB, however many recordings it holds. To give voice types their own recordings, list them in the folder's [`model.toml`](#models-that-bring-their-own-voice-types), one recording each, rather than making a folder per voice type: every folder is a model of its own in memory.
- **Stand-in voices for the voice types you have no trained voice for** are what [fill-in voices](voices.md#fill-in-voices) are for: a `model.toml` with `fill_in = true` maps recordings onto voice types without ever taking one from a voice archive named after it.
- **After adding or replacing a recording, run `autts reload`.** Lines made with that recording are made again; the other recordings' lines stay cached.
- **It is faster than Kokoro**: about 0.3 s of work per second of speech on two threads, against about 0.45 s; see [Cost](what-to-expect.md#cost).

## Models that bring their own voice types

A model can list the voice types it speaks for in `models\<folder>\model.toml`. Installing it is then all it takes, and removing it removes its voice types too. Most voice archives need no such file; it is for:

- a folder not named like a voice type,
- one voice for several voice types, such as `MaleNord` for `MaleNordCommander` too,
- a model holding several voices, each for voice types of its own.

```toml
# voice type editor ID = voice number inside this model
[voicetype]
MaleNord = 0
MaleNordCommander = 0
MaleGuard = 0

# optional, for every voice type listed above
[voice]
speed = 1.0
volume = 1.0
```

- **Your config has the last word**: `[voicetype]` and your presets are asked first; see [the lookup order](voices.md#voice-types).
- **Two models, one voice type**: the one later in folder name order is used, and `AudioUtilTTS.log` names both.
- **Read at game start and on `autts reload`.**

## How many fit in memory

A model takes memory only while it is loaded.

- The **default model** is loaded at game start and stays.
- **Any other model** is loaded the first time a line asks for it, which delays that line by about a second.
- `[model] max_loaded` (4) caps how many **other** models are kept. Past it, the one used longest ago is unloaded. The default model and those named in `[model] keep_loaded` are never unloaded and don't count (shipped as `"pocket-tts"`: a Pocket model serves many voices, so it should stay).

| `max_loaded` | Models in memory, with the default model | and a Pocket model too |
|---|---|---|
| 4 (default) | 5, about 3.1 GB | 6, about 3.7 GB |
| 6 | 7, about 4.4 GB | 8, about 5.0 GB |
| 10 | 11, about 6.9 GB | 12, about 7.5 GB |

A Kokoro model takes about 625 MB once settled, a Piper voice about 350 MB, a Pocket model about 600 MB with all its voices. With one model per voice type, raise `max_loaded` to the number of voices a busy scene mixes, or reloading will delay lines.

## The default model's settings

`[model] family`, `lexicon` and `lang` describe the default model only; other models are read by what their folder holds. `family = "auto"` means Pocket when the folder has an `lm_main*.onnx`, Kokoro when it has `voices.bin`, VITS otherwise. `lexicon` and `lang` are for multi-language Kokoro builds. All keys: [Configuration Reference](configuration.md#model).
