# Mod Events

The simplest way to make AudioUtilTTS speak needs no dependency on it at all: send a mod event. Without the plugin installed the event simply goes nowhere.

!!! warning "Use `SendModEvent`, the Form function"
    Send these with `SendModEvent` on a Form, not with the `ModEvent.Create` / `ModEvent.Send` pair.

## Events AudioUtilTTS listens for

### AudioUtilTTS_Speak

Narration: a line with no speaker, played flat.

| | |
|---|---|
| Sender | Any Form |
| `strArg` | The text to speak |
| `numArg` | Not used |

```papyrus
SendModEvent("AudioUtilTTS_Speak", "The road to Whiterun lay open.")
```

It is spoken with the configured default voice. To choose who speaks, name a preset after a colon:

```papyrus
SendModEvent("AudioUtilTTS_Speak:whisper", "Not so loud.")
```

A preset is a voice the user defines in the config under one name; see [Presets](../players/voices.md#presets). An unknown name falls back to the default voice, so a mod can ask for a role without knowing what the user has installed.

### AudioUtilTTS_Say

Character speech: the sending actor says the line. It is positioned at them and their mouth moves.

| | |
|---|---|
| Sender | **The actor who speaks.** An event from anything else is ignored, with a note in the log |
| `strArg` | The text to say |
| `numArg` | Not used |

```papyrus
akSpeaker.SendModEvent("AudioUtilTTS_Say", "Not so loud.")
akSpeaker.SendModEvent("AudioUtilTTS_Say:whisper", "Not so loud.")
```

With no preset named, the speaker gets the voice set up for their voice type, else the preset `female` or `male` by their sex when the config defines those. See [Voice Types](../players/voices.md#voice-types).

### What an event cannot tell you

An event gives no answer. A line that finds its lane busy is dropped (see [What to design around](overview.md#what-to-design-around)), and the sender does not learn of it. When you need to know, call the [natives](papyrus-api.md) instead: they return `false` for a line that was not taken.

## Events AudioUtilTTS emits

### AudioUtilTTS_Spoken

Fired when a narration or character line starts playing.

| | |
|---|---|
| Sender | The speaking actor, or `None` for narration |
| `strArg` | The text, as spoken |
| `numArg` | The AudioUtil instance handle of the line |

The handle works with AudioUtil's handle functions: `AudioUtil.StopHandle`, `IsHandlePlaying`, `SetHandleVolume`, `GetHandleDuration`.

Dialogue lines do not raise it. The game plays those itself, like recorded lines.

## Examples

Speaking, with no dependency:

```papyrus
; narration
SendModEvent("AudioUtilTTS_Speak", "The road to Whiterun lay open.")
SendModEvent("AudioUtilTTS_Speak:narrator", "Night fell over the hold.")

; a character
akSpeaker.SendModEvent("AudioUtilTTS_Say", "Not so loud.")
```

Listening for the moment a line starts:

```papyrus
Event OnInit()
    RegisterForModEvent("AudioUtilTTS_Spoken", "OnTTSSpoken")
EndEvent

Event OnTTSSpoken(string eventName, string strArg, float numArg, Form sender)
    Actor speaker = sender as Actor     ; None for narration
    int handle = numArg as int
    float seconds = AudioUtil.GetHandleDuration(handle)
    ; strArg is the text now being spoken
EndEvent
```

The example calls an AudioUtil function, so it needs AudioUtil's scripts to compile.

## Trying the events in game

The console harness sends the same events, so you can check the path without writing a script. See [Console Commands](../players/console-commands.md#the-way-other-mods-do-it).

```
autts event "Some text"
autts eventas "Some text" whisper
autts sayevent "Some text"
```
