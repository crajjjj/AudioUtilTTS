# Voice Models

A model is a folder of files the speech runtime reads. The default one, `kokoro-en-v0_19`, comes as the second release archive. You can add more, and a voice archive is nothing else: one such folder, ready to install.

## Adding a model

Models go into their own folder under:

```
Data\SKSE\Plugins\AudioUtilTTS\models\<folder>
```

**A folder named like a voice type needs nothing else.** `models\MaleNord` speaks for the voice type `MaleNord` as soon as it is installed: no line in `AudioUtilTTS.toml`, and no file inside the folder. That is how a voice archive named after a voice type works, and why such an archive holds no `model.toml`.

A folder under any other name does nothing until something names it:

- make it the default with `[model] dir = "<folder>"`,
- or give it a [preset](voices.md#presets) with `model = "<folder>"`,
- or let it speak for voice types, through its own [`model.toml`](#models-that-bring-their-own-voice-types), a [voice type patch](voices.md#voice-type-patches) or a [`[voicetype]`](voices.md#voice-types) line.

Then run `autts reload`.

## The default voice is optional

From 0.3.1 on the default model does not have to be installed. Without it:

- **Every voice with a model of its own speaks as usual**: the unvoiced dialogue and the character lines of the voice types you have voices for.
- **Lines that name no other model are not spoken**: narration, the shipped presets (`narrator`, `female`, `male`, `whisper`) and with them the generic voices of `[dialogue] fallback`.
- **`autts status` starts with `ready | no default model`** and names the reason.
- **It saves memory.** The default model takes about 0.6 GB and one of the `max_loaded` places; without it all of them go to your voices.

With 0.3.0 and older, no dialogue is spoken without the default model.

## Which models work

Two families are supported:

- **Kokoro**: `kokoro-en-v0_19`, the default. One model, many voices.
- **VITS**, such as the **Piper** voices from the [sherpa-onnx model list](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models). Most have a single voice. They are several times faster than Kokoro; see [Cost](what-to-expect.md#cost).

`kokoro-en-v0_19` and `vits-piper-en_US-lessac-medium` have been checked outside the game, loaded together. Larger multi-language Kokoro builds are on the same model list but have not been tried with this plugin.

A voice does not have to come from that list. For making one of your own, see [Training a Voice](../authors/training-voices.md).

!!! warning "Check a new model outside the game first"
    A model that the runtime cannot read can close the game while loading, because the runtime exits on some malformed files. Try a new model with the probe before putting it in a load order; see [Tools](../authors/tools.md#the-probe).

## Models that bring their own voice types

Added in 0.2.1. A model can say which voice types it speaks for, in a file inside its own folder, `model.toml`. Installing the model is then all it takes, and removing the model removes its voice types with it: nothing has to be written into `AudioUtilTTS.toml`.

**Most voice archives have no such file and need none.** A folder named like a voice type already speaks for that voice type. The file is for the cases the folder name cannot cover:

- the folder is not named like a voice type,
- one voice should speak for several voice types, such as `MaleNord` for `MaleNordCommander` too,
- the model holds several voices, each for voice types of its own.

The file is `models\<folder>\model.toml`:

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

What matters as a user:

- **Your config has the last word.** `[voicetype]` and the presets in `AudioUtilTTS.toml` are asked first, so you can overrule a model, or silence a voice type with `Name = ""`.
- **Two models, one voice type.** When two installed models list the same voice type, the one later in folder name order is used, and `AudioUtilTTS.log` names both. A `[voicetype]` line in your config settles it.
- **A folder named like a voice type needs no file.** `models\MaleNord` already speaks for `MaleNord`; the file is for every further voice type.
- **Read at game start and on `autts reload`.** `autts voice` shows the result for the selected NPC ("listed by model ...").

## How many fit in memory

A model takes memory only while it is loaded. An installed model that no line has asked for takes none.

- The **default model**, when it is installed, is loaded at game start and never unloaded.
- **Any other model** is loaded the first time a line asks for it. Loading takes about a second, so that one line is later by that much.
- `[model] max_loaded` caps how many are kept, the default one included (4 by default, 1 to 32). Past the limit the least recently used one is unloaded, never the default. So with the default model installed, three other voices fit at once; without it, four.

Measured per loaded model: a Kokoro model 450 to 500 MB after its first lines and about 625 MB once settled; a Piper voice about 350 MB.

| `max_loaded` | Kokoro models, settled |
|---|---|
| 4 (default) | about 2.5 GB |
| 6 | about 3.8 GB |
| 10 | about 6.3 GB |

With one model per voice type, raise `max_loaded` to the number of voices a busy scene mixes, or loading and unloading will delay lines. One model holding many voices costs the memory once.

## Replacing a model

After replacing a model's files, run `autts reload`. Dialogue lines are cached per model file, so the lines of the old model are not played again.

## The default model's settings

`[model]` describes the default model only:

- `family = "auto"` reads the folder: `voices.bin` present means Kokoro, otherwise VITS. Set `"kokoro"` or `"vits"` to say it outright.
- `lexicon` and `lang` are only for models that need them (multi-language Kokoro builds).

A model named by a preset or a voice type is always read by what its folder contains. The full key list is in the [Configuration Reference](configuration.md#model).
