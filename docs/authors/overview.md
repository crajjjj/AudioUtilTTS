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

- **Speech starts late.** A line is generated when it is asked for: about two seconds for a sentence with the default model, longer on a busy machine. Do not time anything tightly against it. The mod event [`AudioUtilTTS_Spoken`](modevents.md#audioutiltts_spoken) tells you when a line actually starts.
- **A line can be dropped.** By default each lane takes one line at a time: a line that arrives while another is being generated or spoken is dropped. A mod that sends lines faster than they can be spoken is then not read out back to back. Send the line that matters, not every line. The natives return `false` for a line that was not taken; an event gives no answer. If you cut a line yourself (`AudioUtil.StopHandle` with the handle from `AudioUtilTTS_Spoken`, or `StopChannel`), its lane is free at once with AudioUtil 0.9.28 or newer; with an older one it stays busy until the line would have ended, so prefer `AudioUtilTTS.StopSpeaking()`, which frees it on any version.
- **How often you speak is yours to decide.** The plugin has no cooldown of its own. If your mod produces text in bursts, pick what is worth saying on your side.
- **Ask for a role, not a voice.** Name a [preset](../players/voices.md#presets) such as `narrator` or `whisper`. The user decides what that name sounds like, and an unknown name falls back to the default voice, so you never need to know what is installed. For roles of your own, [bring your presets](#bringing-your-own-presets) as a file.
- **Long text is cut.** Text longer than `[playback] max_chars` (400 by default) is cut at the last sentence that fits.
- **The user owns the volume.** Narration follows AudioUtil's `narration` group and character speech the `speech` group, on top of the per-line volume.
- **The mouth is shared.** Character speech moves the mouth through AudioUtil's lipsync, and AudioUtil's guards apply: a gag, a worn tongue item, a dialogue with the player or another mod's claim on the mouth all keep it still. If your mod owns the actor's face, pass `abLipSync = false` to `SpeakActorFrom`.
- **The speaker has to be there.** A character line is dropped if the speaker is gone or unloaded by the time it is ready.

## Bringing your own presets

Added in 0.3.2. The four shipped presets are lines in the user's `AudioUtilTTS.toml`, and that file is the user's: a mod that shipped its own copy would replace their whole config. Ship a preset file instead, under a name of your own:

`Data\SKSE\Plugins\AudioUtilTTS\presets\MyMod.toml`:

```toml
[preset.mymod_stern]
voice = 9
speed = 0.9

[preset.mymod_witch]
model = "my-voices"
voice = 3
```

```papyrus
SendModEvent("AudioUtilTTS_Speak:mymod_stern:MyMod", "You were told to leave.")
akSpeaker.SendModEvent("AudioUtilTTS_Say:mymod_witch:MyMod", "Not so loud.")
```

- **Prefix the names with your mod's name.** Presets share one list. A name two preset files define goes to the file later in name order, and a name the user's config defines stays the user's.
- **The user can change your preset** by defining one of the same name in `AudioUtilTTS.toml`. That is by design: you name the role, they have the last word on its voice.
- **A preset can set a model, a voice number, a speed and a volume**, nothing else. There are no tags for tone or emotion: how a line sounds is the voice you pick. See [Presets](../players/voices.md#presets) for the voices of the default model.
- **A model your preset names does not have to be installed.** Without it the preset speaks with the default voice, and `AudioUtilTTS.log` lists the missing model at game start. Say on your download page which voices your presets are made for.
- **With a plugin older than 0.3.2 the file is not read**, and your names fall back to the default voice like any unknown name. `HasPreset` tells whether a name is known.

For a character of your own who should always sound the same, a preset is not needed: give them a voice type of their own and ship a [voice type patch](../players/voices.md#voice-type-patches). Character speech that names no preset then finds that voice by itself.

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
