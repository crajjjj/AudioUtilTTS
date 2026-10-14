# Console Commands

The `autts` commands need [ConsoleUtil Extended](https://www.nexusmods.com/skyrimspecialedition/mods/133569). Text goes in quotes. Commands about a character act on the actor selected in the console (click them first), or on you when nothing is selected.

## Speaking

| Command | Does |
|---|---|
| `autts say "text"` | Speak a line in the default voice |
| `autts sayv "text" 9` | Speak a line in voice 9 of the default model |
| `autts sayas "text" whisper` | Speak a line through the preset `whisper` |
| `autts sayfrom "text" MyMod` | Speak a line with a sender name, which the log shows |
| `autts sayactor "text"` | Have the selected actor say a line |
| `autts sayactoras "text" whisper` | The same through the preset `whisper` |
| `autts free` | Whether a narration line and a character line would be taken right now |
| `autts stop` | Drop waiting lines and cut the current one |

The answer is `queued`, or `not queued` and why: usually a line still on its way (see [`max_pending`](configuration.md#max_pending)), or a voice whose model is not installed.

## The way other mods do it

These send the mod events a mod would.

| Command | Does |
|---|---|
| `autts event "text"` | Narration through the `AudioUtilTTS_Speak` event |
| `autts eventas "text" whisper` | The same with the preset `whisper` |
| `autts sayevent "text"` | The selected actor speaks through the `AudioUtilTTS_Say` event |

## Voices, dialogue, status

| Command | Does |
|---|---|
| `autts voice` | The selected actor's voice type and the voice it gets |
| `autts dialogue` | The state of unvoiced dialogue and this session's counts |
| `autts dialogue 0` | Switch unvoiced dialogue off for this session (`1` = on) |
| `autts clearcache` | Delete the cached dialogue lines |
| `autts status` | Whether speech is ready, and why not |
| `autts reload` | Read the config again and reload the models |
| `autts enable 0` | Switch all speech off for this session (`1` = on) |

`autts status` on a working install:

```
ready | model kokoro-en-v0_19 (11 voices, 24000 Hz) | voice 7 | 4 presets | 1 of 12 models loaded | ...
```

`1 of 12` counts the voice models in memory against those installed. The `not ready:` answers are listed in [Troubleshooting & Logs](troubleshooting.md#what-autts-status-says). `autts` is short for `AudioUtilTTSTest`, which works too.
