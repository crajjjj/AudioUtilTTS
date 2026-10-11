# Shipping a Voice Model

A voice model can be released as a mod of its own. It is one folder; with a small file inside, installing it is all a user has to do, and removing it removes its voice types with it. Nothing has to be written into `AudioUtilTTS.toml`.

## What a model folder holds

```
Data\SKSE\Plugins\AudioUtilTTS\models\<folder>\
  model.onnx            the model (any single .onnx works; model.onnx is preferred)
  tokens.txt
  voices.bin            Kokoro models only
  espeak-ng-data\       or a lexicon file, for models that use one
  model.toml            optional: the voice types this model speaks for
```

- **The family is read off the folder.** `voices.bin` present means Kokoro, otherwise VITS (the Piper voices).
- **The folder name is the model's name.** It is what a preset's `model = "..."` refers to. Keep it one plain folder name; case is ignored.
- **A folder named exactly like a voice type speaks for that voice type** with no further file: `models\MaleNord` speaks for `MaleNord` with its first voice.

Kokoro (`kokoro-en-v0_19`, the default) and VITS models such as the Piper voices from the [sherpa-onnx model list](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models) are supported. To make a model of your own, see [Training a Voice](training-voices.md).

## model.toml

Added in 0.2.1. The file names the voice types the model speaks for:

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

- **The key is the voice type's editor ID**, as xEdit shows it. Case is ignored.
- **The value is a voice number inside this model**, 0 or more. A model with one voice uses `0` everywhere. A model holding many voices gives each voice type its number.
- **`[voice]` is optional** and applies to every voice type the file lists.
- **The file is read at game start and on `autts reload`.** The log notes it: `Model '<folder>' lists N voice types in its model.toml`.

### Who wins

A voice type finds its voice in a fixed order, and the user's own config is asked first:

1. the user's `[voicetype]` line
2. a preset named exactly like the voice type
3. **your `model.toml`**
4. a model folder named exactly like the voice type
5. the generic voice by sex, when the user has `[dialogue] fallback` on

So a user can always overrule a model, or silence a voice type with `Name = ""`.

When two installed models list the same voice type, the one later in folder name order is used, and `AudioUtilTTS.log` names both. Pick a folder name of your own, and list only the voice types your voice is meant for.

## Checking it

Outside the game, the probe loads the model through the same code the plugin uses and speaks a line. Do this before anything else: a model the runtime cannot read can close the game while loading. See [Tools](tools.md#the-probe).

```
xmake run probe <model folder> "Some text" 0 out.wav
```

In game, click an NPC of a voice type you listed and run:

```
autts voice
autts sayactor "Some text"
```

`autts voice` answers with `listed by model '<folder>'` when your file is what gave that NPC their voice.

## Packaging

The archive should unpack straight into `Data\`, so that the folder lands at `SKSE\Plugins\AudioUtilTTS\models\<folder>`.

If you build from this repo, put the model folder under `dist\SKSE\Plugins\AudioUtilTTS\models\` and run:

```
xmake build package
```

It writes one `Release\AudioUtilTTS-Voice-<folder>.zip` per model folder, `model.toml` included.

!!! warning "Every folder under `models` is packaged"
    `xmake build package` zips each model folder it finds in `dist`. Keep a model you do not mean to release out of that folder.

## What to tell your users

- **It needs AudioUtilTTS 0.2.1 or newer** for `model.toml`. With an older plugin only a folder named like a voice type is picked up.
- **Memory.** Each loaded model costs memory, and `[model] max_loaded` (3 by default) caps how many are kept. A pack of many single-voice models should say so; see [How many fit in memory](../players/models.md#how-many-fit-in-memory). One model holding many voices costs the memory once.
- **The license.** Voice models carry their own license. Put it in the model folder.
