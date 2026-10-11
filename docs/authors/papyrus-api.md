# Papyrus API

Everything is in one script, `AudioUtilTTS.psc` (`Hidden`, all functions `global`). Call the natives when you need a voice number, volume or speed per line, or want to know whether a line was accepted. For the plain case a [mod event](modevents.md) is enough and needs no dependency.

## Guarding the calls

Guard the calls so they never run without the plugin:

```papyrus
If SKSE.GetPluginVersion("AudioUtilTTS") >= 0
    AudioUtilTTS.SpeakAs("Not so loud.", "whisper")
    AudioUtilTTS.Speak("The road to Whiterun lay open.", 9)
    AudioUtilTTS.SpeakActor(akSpeaker, "Not so loud.")
EndIf
```

`GetAPIVersion()` tells what the installed plugin offers: `1` is the first release, `2` (0.2.0) added the dialogue functions and `GetActorVoice`.

## Speaking

Every function here returns `true` when the line was queued and `false` when it was not: the plugin is not ready or switched off, the text holds nothing speakable, or another line is in the way. The line is generated in the background, so it starts a moment later.

```papyrus
bool Function Speak(string asText, int aiVoice = -1, float afVolume = -1.0, float afSpeed = -1.0) global native
```

Narration on the default model. Arguments below zero take the configured defaults. `aiVoice` is a voice number inside the default model; `GetVoiceCount()` says how many it has.

```papyrus
bool Function SpeakAs(string asText, string asPreset) global native
```

Narration through a named preset. A preset can name a different model; that model is loaded the first time a line asks for it (the line is about a second later that once) and then stays in memory. An empty or unknown preset name uses the default voice.

```papyrus
bool Function SpeakActor(Actor akSpeaker, string asText, string asPreset = "", bool abLipSync = true) global native
```

Makes an actor say a line: positioned at them, on the character channel, with their mouth moving when `abLipSync` is true and `[speech] lipsync` is on.

- The mouth follows the loudness of the line; it does not form real phonemes. AudioUtil's own guards still apply (a gag, a worn tongue, a dialogue with the player and a mouth claim all keep it still).
- An empty `asPreset` picks the voice of the speaker's voice type when one is set up, else `female` or `male` by the speaker's sex when the config defines those. `GetActorVoice` tells which.
- The line is dropped if the speaker is gone or not loaded by the time it is ready.

!!! note "One line at a time"
    By default a line that arrives while another line of its kind is waiting, being generated or being spoken is dropped and noted in `AudioUtilTTS.log`. Narration and character lines are counted separately. The user can let lines wait with `[playback] max_pending`; see the [Configuration Reference](../players/configuration.md#max_pending).

## Stopping

```papyrus
Function CancelPending() global native
```

Drops every narration and character line that is waiting or being generated. The line already playing carries on.

```papyrus
Function StopSpeaking() global
```

Drops waiting lines and cuts whatever is being spoken, narration and characters. It is `CancelPending()` plus `AudioUtil.StopChannel` on both channels.

Neither touches dialogue: a dialogue line the game is already saying is the game's own and carries on.

## Asking

| Function | Returns |
|---|---|
| `int GetAPIVersion()` | API version of the installed plugin |
| `bool IsReady()` | True when a line can be spoken right now: the default model is loaded, AudioUtil is present, a playable slot exists and speech is switched on |
| `bool IsBusy()` | True while a narration or character line is waiting or being generated |
| `string GetStatus()` | One line of status text: `ready` followed by the details, or `not ready: <why>` |
| `bool HasPreset(string asPreset)` | True when `AudioUtilTTS.toml` defines `[preset.<asPreset>]` |
| `int GetVoiceCount()` | Number of voices in the default model (0 while it is not loaded) |
| `string GetActorVoice(Actor akSpeaker)` | Which voice the actor speaks with when no preset is named, and why, as one line of text: `MaleNord: preset 'malenord' (model my-voices, voice 12)`. API 2 |
| `string GetChannel()` | The AudioUtil channel narration plays on (`[playback] channel`) |
| `string GetSpeechChannel()` | The AudioUtil channel character speech plays on (`[speech] channel`) |

`GetStatus` and `GetActorVoice` return text meant for a person: show it, do not parse it.

## Switching

```papyrus
Function SetEnabled(bool abEnabled) global native
bool Function IsEnabled() global native
```

A session switch on top of `[general] enable`. Switching off drops waiting lines.

```papyrus
bool Function ReloadConfig() global native
```

Re-reads `AudioUtilTTS.toml` and reloads the models. `false` means the file did not parse and the previous settings were kept.

## Dialogue

API 2. Nothing here has to be called for dialogue to be spoken: the plugin does that by itself when `[dialogue] enable` is true. See [Unvoiced Dialogue](../players/unvoiced-dialogue.md).

| Function | Does |
|---|---|
| `Function SetDialogueEnabled(bool abEnabled)` | Session switch on top of `[dialogue] enable`. It cannot switch the feature on when the config had it off at game start: the game is only patched then |
| `bool IsDialogueEnabled()` | True when unvoiced dialogue is being spoken: enabled in the config, patched into the game and not switched off |
| `string GetDialogueStatus()` | One line of status text: on or off and why, lines voiced and left unvoiced this session, and the size of the cache |
| `int ClearDialogueCache()` | Deletes every cached dialogue line, so each is generated afresh the next time it is said. Returns the number of files removed. Not needed after replacing a model: lines are cached per model file, and `ReloadConfig` picks up the change |

## When a line starts

The mod event `AudioUtilTTS_Spoken` fires when a narration or character line starts playing, with the text, the AudioUtil instance handle and the speaking actor. See [Mod Events](modevents.md#audioutiltts_spoken).

## The console harness

`AudioUtilTTSTest.psc` wraps these functions as console commands (`autts ...`) through ConsoleUtil Extended. It is a working example of every call; see [Console Commands](../players/console-commands.md).
