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

The multi-language Kokoro builds on the same list have not been tried with this plugin.

To make a voice of your own, see [Training a Voice](../authors/training-voices.md).

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
- `[model] max_loaded` (4) caps how many are kept, the default one included. Past it, the one used longest ago is unloaded, never the default.

| `max_loaded` | Kokoro models in memory |
|---|---|
| 4 (default) | about 2.5 GB |
| 6 | about 3.8 GB |
| 10 | about 6.3 GB |

A Kokoro model takes about 625 MB once settled, a Piper voice about 350 MB. With one model per voice type, raise `max_loaded` to the number of voices a busy scene mixes, or reloading will delay lines.

## The default model's settings

`[model] family`, `lexicon` and `lang` describe the default model only; other models are read by what their folder holds. `family = "auto"` means Kokoro when the folder has `voices.bin`, VITS otherwise. `lexicon` and `lang` are for multi-language Kokoro builds. All keys: [Configuration Reference](configuration.md#model).
