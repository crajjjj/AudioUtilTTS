# Shipping a Voice Model

A voice model can be released as a mod of its own: one folder that a user installs, and nothing to configure.

## What a model folder holds

```
Data\SKSE\Plugins\AudioUtilTTS\models\<folder>\
  model.onnx            the model (any single .onnx works; model.onnx is preferred)
  tokens.txt
  voices.bin            Kokoro models only
  espeak-ng-data\       or a lexicon file, for models that use one
  model.toml            optional: the voice types this model speaks for
```

- **A folder named exactly like a voice type speaks for it** with no further file: `models\MaleNord` speaks for `MaleNord` with its first voice. For one voice and one voice type, that is all.
- **The folder name is the model's name**, what a preset's `model = "..."` refers to. One plain folder name; case is ignored.
- **The family is read off the folder**: an `lm_main*.onnx` means Pocket, `voices.bin` Kokoro, otherwise VITS (Piper voices).

A Pocket model holds other files, and its voices are recordings:

```
Data\SKSE\Plugins\AudioUtilTTS\models\<folder>\
  lm_main*.onnx  lm_flow*.onnx  encoder*.onnx  decoder*.onnx  text_conditioner*.onnx
  vocab.json  token_scores.json
  voices\*.wav          one recording per voice, referred to by its file name
  model.toml            optional, as for any model
```

- **A recording is a voice by its file name** without `.wav`: `voice = "gruff"` in a preset, a patch or the `model.toml`. A number (its place in name order) works too, but is fragile: a recording added by someone else renumbers the rest.
- **Each part may come as `.int8.onnx` or `.onnx`**; with both there, the `.int8` one is used.
- **Every recording is read when the model loads.** One that cannot be read stops the model from loading, and the log names it: skipping it would renumber the voices after it.
- **Recordings can be shipped on their own.** A mod that needs one voice ships `voices\<mod>-<name>.wav` into the Pocket model's folder (a mod manager merges the folders), a [preset file](overview.md#bringing-your-own-presets) or a voice type patch that names it, and lists the Pocket model as a requirement. Prefix the file name with your mod's name, so that it never collides with another mod's recording.
- **Use only voices you have the right to.** Kyutai's terms for Pocket TTS forbid copying a person's voice without their consent. A voice made from someone's recordings needs that person's agreement, and the license that comes with it belongs in the model folder.

See [Pocket voices](../players/models.md#pocket-voices) for what makes a good recording.

## Fill-in voices

A voice archive that gives many voice types a stand-in voice marks them as fill-ins, so that a voice the user installs for one of those voice types wins without any configuration, a voice archive named after it included:

```toml
fill_in = true

[voicetype]
MaleOrc = "gruff"
FemaleOldGrumpy = "old-woman"
```

- The same key works at the top of a voice type patch, or on one of its lines: `{ model = "pocket-tts", voice = "gruff", fill_in = true }`.
- A fill-in ranks below every other way a voice type finds its voice; see [the lookup order](../players/voices.md#voice-types). Among files that name one voice type, one meant for it beats a fill-in whatever the file order.
- The user can switch every fill-in voice off with `[general] fill_in_voices = false`.
- `autts voice` answers `fill-in listed by model '...'` for an NPC who gets one.

To make a model of your own, see [Training a Voice](training-voices.md).

## model.toml

For a folder not named like a voice type, one voice for several voice types, or a model with several voices, the file names the voice types the model speaks for:

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

- **The key is the voice type's editor ID** (as xEdit shows it, any case); **the value a voice number inside this model**: `0` for a model with one voice.
- **`[voice]` is optional** and applies to every voice type listed.
- **Read at game start and on `autts reload`**; the log says `Model '<folder>' lists N voice types in its model.toml`.
- **The user's config and voice type patches come first** in [the lookup order](../players/voices.md#voice-types), so a user can always overrule your file. When two models list the same voice type, the later folder name wins: pick a folder name of your own, and list only the voice types your voice is meant for.

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

- **Versions**: `model.toml` needs AudioUtilTTS 0.2.1 or newer; speaking without the default model installed, 0.3.1 or newer; a Pocket model, a voice named by its recording and fill-in voices, 0.4.1 or newer.
- **Memory**: each loaded model costs memory, and `[model] max_loaded` (4) caps how many are kept; see [How many fit in memory](../players/models.md#how-many-fit-in-memory). One model holding many voices costs it once.
- **The license**: put the voice's license in the model folder.
