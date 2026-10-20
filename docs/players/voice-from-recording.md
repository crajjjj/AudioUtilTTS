# A Voice from a Recording

With the Pocket model installed, a short recording of a voice becomes a voice in the game: a few seconds of someone talking, one file in one folder, one preset. This page walks through it once. The rules behind each step are on [Voice Models](models.md#pocket-voices) and [Voices, Presets & Voice Types](voices.md).

## What you need

- **AudioUtilTTS working**: `autts status` in the console starts with `ready`. See [Getting Started](getting-started.md).
- **The Pocket model**, `AudioUtilTTS-Voice-pocket-tts.zip` from the [latest release](https://github.com/crajjjj/AudioUtilTTS/releases/latest), installed like any mod. It lands in `Data\SKSE\Plugins\AudioUtilTTS\models\pocket-tts` and has no voices of its own.
- **A recording**: 5 to 10 seconds of one person speaking, saved as a wav. No music, no second voice, no echo. Any wav works, mono or stereo, any sample rate; only the first 10 seconds are used. A phone recording of yourself is fine. A recording in another format, or a longer one, is cut and saved as wav in a free editor such as [Audacity](https://www.audacityteam.org/).
- **The right to use that voice.** Kyutai's terms for Pocket TTS forbid copying a person's voice without their consent. Your own voice, or a friend's with their agreement, is yours to use. Kyutai publishes voices recorded for this, each with its license, at [kyutai/tts-voices](https://huggingface.co/kyutai/tts-voices).

The manner of the recording carries over: a whispered recording gives whispered lines, a cheerful one cheerful lines. Record the way you want the character to sound.

## 1. Put the recording in the model's `voices` folder

The file goes here, with the name you want the voice to have:

```
Data\SKSE\Plugins\AudioUtilTTS\models\pocket-tts\voices\anna.wav
```

- **The file name is the voice's name**: `anna.wav` is the voice `anna`. Case is ignored.
- **Create the `voices` folder if it is not there.**
- **With Mod Organizer or Vortex**, put the file in a mod of your own rather than in the model's: a mod that holds just `SKSE\Plugins\AudioUtilTTS\models\pocket-tts\voices\anna.wav`. The game sees both folders as one, and reinstalling the model later leaves your recording alone.
- **Without a mod manager**, `Data` is the game's own `Data` folder.

## 2. Give the voice a name the game can ask for

A **preset** is that name. Open `Data\SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml` in a text editor and add this at the end:

```toml
[preset.anna]
model = "pocket-tts"
voice = "anna"
```

- **The preset name is yours.** It follows `preset.`; `anna` here, but it can differ from the file name.
- **`voice` is the recording's file name** without `.wav`.
- **`volume = 1.0` can be added**; `speed` does nothing on a Pocket voice.
- **Under a mod manager**, edit the copy of `AudioUtilTTS.toml` the game sees: normally the one in the AudioUtilTTS mod's folder, or the copy a voice pack ships, when it ships one. The [in-game menu](settings-menu.md) edits that copy, but adding a new preset is done in the editor.

## 3. Reload and listen

1. In the console, `autts reload`. Or open the [in-game menu](settings-menu.md) and press **Reload settings and voices**.
2. `autts sayas "The road north is closed." anna` speaks the line in the new voice. The menu's Status page does the same under **Try a voice**: type a line, pick `anna`, press **Speak it as narration**.

The first line takes a moment longer: the Pocket model is loaded then, about 0.6 GB of memory, once for every recording it holds.

??? note "Nothing is heard"
    - `AudioUtilTTS.log` lists the recordings the model found when it loaded; `autts status` shows whether it is among the models loaded.
    - A recording the model cannot read stops the whole model from loading, and the log names the file. Save it as a plain wav and reload.
    - `no voice in ...\voices` in the log means the folder is empty or in the wrong place: check the path in step 1.
    - The [Troubleshooting](troubleshooting.md) page covers the rest.

## 4. Give it to a character

A preset alone speaks lines that ask for it by name. To have the NPCs of a voice type speak with it:

- **In the in-game menu**, on the **Voice types** page, under **Add a line**: type the voice type's ID, pick the preset `anna`, press **Add the line**.
- **In `AudioUtilTTS.toml`**, add the line under `[voicetype]`:

```toml
[voicetype]
FemaleYoungEager = "anna"
```

- **The voice type's ID** is what `autts voice` shows with the NPC selected in the console (click them first). The same command then shows which voice they get.
- **Every NPC of that voice type** speaks with it, for the lines that have no recording of their own. The line comes first in the [lookup](voices.md#voice-types), ahead of a voice archive named after the voice type.
- **One character of a mod**, with a voice type of their own, is given a voice the same way: their voice type's ID on the left.

## 5. Use it as the generic voice

The generic female and male voices, which speak every unvoiced voice type with `[dialogue] fallback = true` and your own lines by sex, are the presets `female` and `male` of `AudioUtilTTS.toml`. Point one at a recording, and that recording is the generic voice; no training is involved:

```toml
[preset.female]
model = "pocket-tts"
voice = "anna"
```

- **The shipped `female` and `male` presets** name voices 1 and 9 of the default model. Replace their `voice = N` line with the two lines above; `narrator` and `whisper` change the same way.
- **Run `autts reload`** afterwards. Lines are then made with the new voice.
- **The default voice itself** (`[voice]`, what a line with no preset gets) belongs to the default model. To change it to a recording, give lines a preset instead, or make the Pocket model the default with `[model] dir = "pocket-tts"` and `[voice] voice = "anna"`. Then every preset on the default model needs a recording name of its own.

## More than one voice

- **Each wav in the folder is one voice**, and all of them share the one loaded model. One speaker recorded in several moods is several recordings and several presets.
- **Replacing a recording** keeps its name: overwrite the file and run `autts reload`. Lines made with that recording are made again; the other recordings' lines stay cached.
- **Sharing a voice with others**, as a mod: see [Shipping a Voice Model](../authors/voice-models.md), which also shows how a mod names the voice in a preset file of its own.
