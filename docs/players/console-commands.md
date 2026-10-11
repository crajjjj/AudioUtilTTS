# Console Commands

The `autts` commands need [ConsoleUtil Extended](https://www.nexusmods.com/skyrimspecialedition/mods/133569). Open the console, type a command, and read the answer in the console.

Text goes in quotes. Commands that act on a character use the reference selected in the console (click the NPC first), or the player when nothing is selected. Switch to third person to see your own mouth move.

## Speaking

| Command | Does |
|---|---|
| `autts say "text"` | Speak a line with the configured voice |
| `autts sayv "text" 9` | Speak a line with voice number 9 of the default model |
| `autts sayas "text" whisper` | Speak a line through the preset `whisper` |
| `autts sayfrom "text" MyMod` | Speak a line naming the sender; the name shows up in `AudioUtilTTS.log` |
| `autts sayactor "text"` | Make the selected actor (or the player) say a line |
| `autts sayactoras "text" whisper` | The same through the preset `whisper` |
| `autts free` | Show whether a narration line and a character line would be taken right now |
| `autts stop` | Drop waiting lines and cut the current one |

The answer is `queued`, or `not queued` with the reason. With the plugin ready, the usual reason is a line still being generated or spoken: a second one is dropped, not queued. See [`max_pending`](configuration.md#max_pending).

## The way other mods do it

These send the same mod events a mod would, so you can check that path.

| Command | Does |
|---|---|
| `autts event "text"` | Speak a line through the `AudioUtilTTS_Speak` mod event |
| `autts eventas "text" whisper` | The same with a preset (`AudioUtilTTS_Speak:whisper`) |
| `autts sayevent "text"` | Make the selected actor say a line through the `AudioUtilTTS_Say` mod event |

## Voices and dialogue

| Command | Does |
|---|---|
| `autts voice` | Show the voice type of the selected actor (or the player) and the voice it leads to |
| `autts dialogue` | Show the status of unvoiced-dialogue speech |
| `autts dialogue 0` | Switch unvoiced-dialogue speech off for this session (`1` = on) |
| `autts clearcache` | Delete the cached dialogue lines |

## Status and settings

| Command | Does |
|---|---|
| `autts status` | Show whether speech is ready, and why not |
| `autts reload` | Re-read the config and reload the models |
| `autts enable 0` | Switch speech off for this session (`1` = on) |

`autts status` on a working install reads like this:

```
ready | model kokoro-en-v0_19 (11 voices, 24000 Hz) | voice 7 | 4 presets | 1 models loaded | narration slots 4 of 4 | speech slots 4 of 4, lipsync on | dialogue on | sherpa-onnx ...
```

What the `not ready:` answers mean is listed in [Troubleshooting & Logs](troubleshooting.md#what-autts-status-says).

!!! note "The long form"
    `autts` is an alias. The full command name is `AudioUtilTTSTest`, so `AudioUtilTTSTest say "text"` does the same.
