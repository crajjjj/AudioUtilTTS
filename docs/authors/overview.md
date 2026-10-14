# Overview

AudioUtilTTS turns a line of text into speech in the running game. This page is the map: the ways in, and what to design around.

## Four ways in

| You want | Use | Dependency on this mod |
|---|---|---|
| A line spoken, as simply as possible | A [mod event](modevents.md): `AudioUtilTTS_Speak` or `AudioUtilTTS_Say` | None. Without the plugin the event goes nowhere |
| To know whether the line was taken, or whether one would be | The [Papyrus natives](papyrus-api.md), behind a plugin check | Soft: `AudioUtilTTS.psc` at compile time, a guard at run time |
| The same from an SKSE plugin | The [C++ API](cpp-api.md): exports resolved at run time | None. A copied header, no link dependency |
| Your mod's dialogue spoken | Nothing. Ship the dialogue without voice files | None. See [Unvoiced Dialogue](../players/unvoiced-dialogue.md) |

```papyrus
; narration: flat, no speaker
SendModEvent("AudioUtilTTS_Speak::MyMod", "The road to Whiterun lay open.")

; a character says it: positioned at them, mouth moving
akSpeaker.SendModEvent("AudioUtilTTS_Say::MyMod", "Not so loud.")
```

`MyMod` is your mod's own short name. It goes into `AudioUtilTTS.log` with each of your lines, so a user can see which mod a spoken or dropped line came from.

## Narration or character speech

| | Narration | Character speech |
|---|---|---|
| Sent by | Any Form | The actor who speaks |
| Sound | Flat, not positioned | Positioned at the actor |
| Mouth | None moves | The speaker's moves, driven by loudness |
| Voice when no preset is named | The configured default voice | The voice of the speaker's voice type, else `female` / `male` by sex |
| AudioUtil group and channel | `narration` | `speech` |
| Subtitles | None | None |

The two run on separate lanes with their own channel and files, so a character and the narrator can talk at once.

## What to design around

- **Speech starts late**: about two seconds a sentence with the default model, longer on a busy machine. Do not time anything tightly against it; the event [`AudioUtilTTS_Spoken`](modevents.md#audioutiltts_spoken) tells you when a line starts.
- **A line can be dropped.** By default each lane takes one line at a time, and a line that arrives while another is generated or spoken is dropped. Send the line that matters, not every line. The natives return `false` for a line not taken; an event gives no answer. To cut your own line, `AudioUtilTTS.StopSpeaking()` frees the lane on any AudioUtil version.
- **How often you speak is yours to decide.** The plugin has no cooldown of its own.
- **Ask for a role, not a voice.** Name a [preset](../players/voices.md#presets) such as `narrator` or `whisper`: the user decides what it sounds like, and an unknown name falls back to the default voice. For roles of your own, [bring your presets](#bringing-your-own-presets).
- **Long text is cut** at the last sentence that fits into `[playback] max_chars` (400).
- **The user owns the volume**: AudioUtil's `narration` and `speech` groups, on top of the per-line volume.
- **The mouth is shared.** AudioUtil's guards apply (a gag, a worn tongue item, a dialogue with the player, another mod's claim on the mouth). If your mod owns the actor's face, pass `abLipSync = false` to `SpeakActorFrom`.
- **The speaker has to be there**: a character line is dropped if the speaker is gone by the time it is ready.

## Bringing your own presets

`AudioUtilTTS.toml` is the user's file: a mod that shipped its own copy would replace their whole config. Ship a preset file instead, named after your mod:

`Data\SKSE\Plugins\AudioUtilTTS\presets\MyMod.toml`:

```toml
[preset.mymod_stern]
voice = 9
speed = 0.9

[preset.mymod_witch]
model = "my-voices"
voice = 3
```

Then ask for them by name:

```papyrus
SendModEvent("AudioUtilTTS_Speak:mymod_stern:MyMod", "You were told to leave.")
akSpeaker.SendModEvent("AudioUtilTTS_Say:mymod_witch:MyMod", "Not so loud.")
```

- **Prefix the names with your mod's name**: presets share one list. Use letters, digits, `_` and `-` only; a dot makes a nested table, and a colon separates preset and sender in the event name.
- **The user has the last word**: a preset of the same name in their `AudioUtilTTS.toml` replaces yours. Between two preset files, the later in name order wins.
- **A preset sets a model, a voice number, a speed and a volume**, nothing else; there are no tone or emotion tags. Without a model it speaks on the user's default model, whose voice numbers are listed under [Voices](../players/voices.md#voices). A number a model does not have becomes its last voice.
- **The model you name need not be installed.** Without it, your line is spoken in the user's default voice at your speed and volume; without that too, it is not spoken. Say on your download page which voices your presets are made for.
- **A preset is for lines that ask for it by name**, never the voice of a voice type by being named like one. For a character who should always sound the same, give them a voice type of their own and ship a [voice type patch](../players/voices.md#voice-type-patches).
- **Plugins older than 0.3.2 do not read the file**, and your names fall back to the default voice. [`AudioUtilTTS.HasPreset("mymod_stern")`](papyrus-api.md#asking) tells whether the installed plugin knows the name.

**Checking it.** `AudioUtilTTS.log` has one line per preset file at game start and on `autts reload`, `Preset file MyMod.toml: 2 new presets`, with the names the user's config overruled and the models not installed. A file that does not parse is skipped, and the log names the line. `autts sayas "Some text" mymod_stern` speaks through the preset. Under Mod Organizer 2, a file added while the game runs is not seen until the next start.

## Making and releasing a voice

A voice of your own is a model folder like any other. [Training a Voice](training-voices.md) describes a route that has worked: fine-tuning Kokoro on one speaker and exporting the result to the format the plugin loads.

A voice model can be released as a mod of its own: a model folder with a `model.toml` naming the voice types it speaks for. Installing it is all a user has to do. See [Shipping a Voice Model](voice-models.md).

## Where things live in the source tree

```
src/
  main.cpp            SKSE entry point, logging, the load pipeline
  Config.*            AudioUtilTTS.toml, the model.toml files, voice type patches and preset files
  TtsEngine.*         the speech runtime: load a model, generate a line
  Speaker.*           the lanes, the queue, voices and presets, the dialogue cache
  DialogueHook.*      the patch that hands generated lines to the game's dialogue
  Greetings.*         greetings made ahead
  Lip.*               writes the mouth animation of a dialogue line
  PapyrusAPI.*        the natives of AudioUtilTTS.psc
  AudioUtilTTSAPI.cpp the C++ exports for other SKSE plugins
include/API/          AudioUtilTTSAPI.h, the reference header for those exports
papyrus/Source/       AudioUtilTTS.psc (the API), AudioUtilTTSTest.psc (the console harness)
tools/probe/          the offline probe
tools/voicetrain/     the voice training kit
tools/dbvo/           the voice pack gap filler
```

More in [How It Works](how-it-works.md) and [Building from Source](building.md).
