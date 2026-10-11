# Overview

AudioUtilTTS turns a line of text into speech in the running game. This page is the map: the ways in, and what to design around.

## Three ways in

| You want | Use | Dependency on this mod |
|---|---|---|
| A line spoken, as simply as possible | A [mod event](modevents.md): `AudioUtilTTS_Speak` or `AudioUtilTTS_Say` | None. Without the plugin the event goes nowhere |
| To know whether the line was taken, or to set voice number, volume or speed per line | The [Papyrus natives](papyrus-api.md), behind a plugin check | Soft: `AudioUtilTTS.psc` at compile time, a guard at run time |
| Your mod's dialogue spoken | Nothing. Ship the dialogue without voice files | None. See [Unvoiced Dialogue](../players/unvoiced-dialogue.md) |

```papyrus
; narration: flat, no speaker
SendModEvent("AudioUtilTTS_Speak", "The road to Whiterun lay open.")

; a character says it: positioned at them, mouth moving
akSpeaker.SendModEvent("AudioUtilTTS_Say", "Not so loud.")
```

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
- **A line can be dropped.** By default each lane takes one line at a time: a line that arrives while another is waiting, being generated or being spoken is dropped. A mod that sends lines faster than they can be spoken is then not read out back to back. Send the line that matters, not every line. The natives return `false` for a line that was not taken; an event gives no answer.
- **How often you speak is yours to decide.** The plugin has no cooldown of its own. If your mod produces text in bursts, pick what is worth saying on your side.
- **Ask for a role, not a voice.** Name a [preset](../players/voices.md#presets) such as `narrator` or `whisper`. The user decides what that name sounds like, and an unknown name falls back to the default voice, so you never need to know what is installed.
- **Long text is cut.** Text longer than `[playback] max_chars` (400 by default) is cut at the last sentence that fits.
- **The user owns the volume.** Narration follows AudioUtil's `narration` group and character speech the `speech` group, on top of the per-line volume.
- **The mouth is shared.** Character speech moves the mouth through AudioUtil's lipsync, and AudioUtil's guards apply: a gag, a worn tongue item, a dialogue with the player or another mod's claim on the mouth all keep it still. If your mod owns the actor's face, pass `abLipSync = false` to `SpeakActor`.
- **The speaker has to be there.** A character line is dropped if the speaker is gone or unloaded by the time it is ready.

## Releasing a voice

A voice model can be released as a mod of its own: a model folder with a `model.toml` naming the voice types it speaks for. Installing it is all a user has to do. See [Shipping a Voice Model](voice-models.md).

## Where things live in the source tree

```
src/
  main.cpp            SKSE entry point, logging, the load pipeline
  Config.*            AudioUtilTTS.toml and the model.toml files
  TtsEngine.*         the speech runtime: load a model, generate a line
  Speaker.*           the lanes, the queue, voices and presets, the dialogue cache
  DialogueHook.*      the patch that hands generated lines to the game's dialogue
  Greetings.*         greetings made ahead
  Lip.*               writes the mouth animation of a dialogue line
  PapyrusAPI.*        the natives of AudioUtilTTS.psc
papyrus/Source/       AudioUtilTTS.psc (the API), AudioUtilTTSTest.psc (the console harness)
tools/probe/          the offline probe
tools/dbvo/           the voice pack gap filler
```

More in [How It Works](how-it-works.md) and [Building from Source](building.md).
