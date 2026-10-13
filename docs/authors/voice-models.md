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
  lines\                optional: ready-made lines, see below
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
3. a [voice type patch](../players/voices.md#voice-type-patches) the user installed (0.3.0 and newer)
4. **your `model.toml`**
5. a model folder named exactly like the voice type
6. the generic voice by sex, when the user has `[dialogue] fallback` on

So a user can always overrule a model, or silence a voice type with `Name = ""`. A patch is the way to give further voice types to a voice you did not make: it is a file outside the model's folder, so it does not replace the model's own `model.toml`.

When two installed models list the same voice type, the one later in folder name order is used, and `AudioUtilTTS.log` names both. Pick a folder name of your own, and list only the voice types your voice is meant for.

## Ready-made lines

Added in 0.3.2. A line is generated the first time it is needed, which takes a moment: a greeting comes late that once, a line said in passing stays unvoiced that once. A model can bring such lines along, generated beforehand:

```
Data\SKSE\Plugins\AudioUtilTTS\models\<folder>\lines\<name>.wav
```

The plugin uses them like lines from its own cache: in place the first time, with nothing generated and the model not even loaded. What the user sees is in [Ready-made lines](../players/unvoiced-dialogue.md#ready-made-lines).

- **A file's name is the line.** It is made from the voice number, the speed and the cleaned text, and from nothing else. It holds nothing of your machine, which is why the files can be handed out. A file under any other name is never looked at.
- **One file per line and voice.** A line of 3 seconds is about 150 KB (mono, 16 bit, at the model's own rate), so 300 lines are about 45 MB.
- **Which lines are worth it.** The ones the game says the moment it decides on them: greetings, the line that opens a conversation, remarks in passing. Lines listed in the dialogue menu are usually ready in time anyway.
- **They belong to the model they were made with.** After training the model anew, make them again; the old files would be the old voice.

There are two ways to make them.

### Collecting them in the game

Set `[dialogue] collect_lines = true`, run `autts reload`, and play. From then on every line the plugin generates because it was not in place in time is also written out, laid out like the models folder:

```
Data\SKSE\Plugins\AudioUtilTTS\collected\
  <folder>\lines\<name>.wav     the lines, ready to hand out
  <folder>\lines.txt            their texts: file name, voice, speed and text, tab separated
```

- **What is collected.** Lines that stayed unvoiced the first time or were spoken late, and greetings made ahead. So `[dialogue] prepare_greetings = true` collects the greetings of the NPCs around you without waiting for each of them to speak.
- **What is not.** Lines that were ready in time, lines that came from the cache, and lines the model brings along already. To collect a line again, clear the cache first (`autts clearcache`).
- **Hand them out** by copying `collected\<folder>\lines` into the model's own folder, or by packing it as an archive of its own that unpacks to `SKSE\Plugins\AudioUtilTTS\models\<folder>\lines`. `lines.txt` stays with you.
- **Read `lines.txt` before you publish.** It lists every collected line, of every mod in your load order, and each row names its file. Delete the files of the lines you do not want to hand out.
- **Collect with your own settings at their defaults**: `[playback] output_rate = 0`, and the speed, `skip_directions` and `max_chars` you expect your users to have. A user with other settings generates the line instead.
- **Under Mod Organizer 2** the `collected` folder is created where your profile puts files the game writes: Overwrite, or the mod you set for that.

### Making them from a list

The probe makes the lines of a list, outside the game:

```
probe --lines <model dir> <list file> [threads]
```

It writes them into `<model dir>\lines` under the names the plugin looks for. The list is UTF-8, one line each: the text alone (voice 0, speed 1), or a row of a collected `lines.txt` as it is. So a list you wrote yourself works, and a collected list gives a retrained model its lines again. See [Tools](tools.md#make-ready-made-lines).

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

It writes one `Release\AudioUtilTTS-Voice-<folder>.zip` per model folder, `model.toml` and a `lines` folder included.

!!! warning "Every folder under `models` is packaged"
    `xmake build package` zips each model folder it finds in `dist`. Keep a model you do not mean to release out of that folder.

## What to tell your users

- **It needs AudioUtilTTS 0.2.1 or newer** for `model.toml`. With an older plugin only a folder named like a voice type is picked up.
- **The default voice.** From 0.3.1 on your voice speaks without the default model installed. With 0.3.0 and older the user needs that one too.
- **Memory.** Each loaded model costs memory, and `[model] max_loaded` (4 by default) caps how many are kept. A pack of many single-voice models should say so; see [How many fit in memory](../players/models.md#how-many-fit-in-memory). One model holding many voices costs the memory once.
- **The license.** Voice models carry their own license. Put it in the model folder.
- **Ready-made lines** need AudioUtilTTS 0.3.2 or newer. An older plugin ignores the folder and generates every line itself.
