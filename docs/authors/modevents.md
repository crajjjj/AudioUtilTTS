# Mod Events

The simplest way to make AudioUtilTTS speak needs no dependency on it at all: send a mod event. Without the plugin installed the event simply goes nowhere.

!!! warning "Use `SendModEvent`, the Form function"
    Send these with `SendModEvent` on a Form, not with the `ModEvent.Create` / `ModEvent.Send` pair.

## Events AudioUtilTTS listens for

### AudioUtilTTS_Speak

Narration: a line with no speaker, played flat. The event name is `AudioUtilTTS_Speak:<preset>:<sender>`.

| | |
|---|---|
| Sent by | Any Form |
| `<preset>` | Who speaks; empty = the configured default voice |
| `<sender>` | Your mod's own short name, for the log |
| `strArg` | The text to speak |
| `numArg` | Not used |

```papyrus
SendModEvent("AudioUtilTTS_Speak::MyMod", "The road to Whiterun lay open.")
SendModEvent("AudioUtilTTS_Speak:whisper:MyMod", "Not so loud.")
```

The first is spoken with the configured default voice, the second with the preset `whisper`. A preset is a voice the user defines in the config under one name; see [Presets](../players/voices.md#presets). An unknown name falls back to the default voice, so a mod can ask for a role without knowing what the user has installed.

The sender name is written into `AudioUtilTTS.log` with the line, so a user can see which mod a spoken or dropped line came from. Nothing else depends on it.

### AudioUtilTTS_Say

Character speech: the sending actor says the line. It is positioned at them and their mouth moves. The event name is `AudioUtilTTS_Say:<preset>:<sender>`.

| | |
|---|---|
| Sent by | **The actor who speaks.** An event from anything else is ignored, with a note in the log |
| `<preset>` | Whose voice; empty = the speaker's own |
| `<sender>` | Your mod's own short name, for the log |
| `strArg` | The text to say |
| `numArg` | Not used |

```papyrus
akSpeaker.SendModEvent("AudioUtilTTS_Say::MyMod", "Not so loud.")
akSpeaker.SendModEvent("AudioUtilTTS_Say:whisper:MyMod", "Not so loud.")
```

With no preset named, the speaker gets the voice set up for their voice type, else the preset `female` or `male` by their sex when the config defines those. See [Voice Types](../players/voices.md#voice-types).

### Older plugin versions

The sender name arrived with 0.2.1. An older plugin reads `:MyMod` as part of the preset name, finds no such preset and speaks the line with the default voice. The line is still spoken; only a preset named in the same event is lost.

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
SendModEvent("AudioUtilTTS_Speak::MyMod", "The road to Whiterun lay open.")
SendModEvent("AudioUtilTTS_Speak:narrator:MyMod", "Night fell over the hold.")

; a character
akSpeaker.SendModEvent("AudioUtilTTS_Say::MyMod", "Not so loud.")
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
