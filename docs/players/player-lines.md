# Your Own Lines

[Dragonborn ReVoiced](https://www.nexusmods.com/skyrimspecialedition/mods/184221) (DBReV) gives your character a voice: when you pick a dialogue topic, it plays the line from its voice pack and holds the conversation until the line is said. A voice pack covers the lines its author recorded, and mod dialogue is mostly not among them. With AudioUtilTTS installed, DBReV hands those lines over, and the plugin speaks them in your character's voice while DBReV keeps the conversation open.

Nothing to set up: with both installed, it is on. DBReV needs a version with its voice provider API (newer than 1.5.2) and a voice pack selected in its menu. With no pack selected DBReV stays out of conversations, and so does this.

## What happens

- **Only lines the pack has no recording for.** A line the pack has is DBReV's as before, with its boost, pitch, effects and delay. A line from here is plain: AudioUtil plays it at your character, mouth moving (`[speech] lipsync`), at the preset's volume. DBReV's post-line delay follows it like any other line.
- **The first time, the conversation waits** while the line is generated: about a second for a short line with the default model, longer for a long line or while another line is being generated. The line is then kept in the cache and said at once every time after.
- **A line that would take longer than `[player] max_wait`** (5 seconds) stays silent that once and is generated for the next time.
- **Skips work.** Skipping the line, picking another topic or leaving the dialogue stops it.
- **Stage directions are left out**, as for NPC lines: `(Persuade)`, `(Intimidate)` and the like are not spoken (`[dialogue] skip_directions`). A line that is nothing but a bracketed action stays silent. Your [text replacements](configuration.md#text-replacements) apply too, for a race name a body mod has changed, say.
- **Camera and expression mods see an ordinary player line.** DBReV announces it as it does its own, with the length this plugin reports.
- **An NPC line spoken late stops** when you begin a line, any line, as the game stops its own lines when you answer. See [Greetings spoken late](unvoiced-dialogue.md#greetings-spoken-late).

## The voice

Your character's voice is `[player] preset`. As shipped that is the preset `bella`: `af_bella`, voice 1 of the default model, the voice closest to the Bella voice packs, whatever your character's sex. With the key empty you get the preset `female` or `male` by your character's sex instead. A voice type is never used, not even your character's: that is a game actor's recorded voice, and your character is whoever you play.

To use a voice of your own, name a preset:

```toml
[player]
preset = "myhero"

[preset.myhero]
model = "my-voices"
voice = 3
```

Any preset works: the ones in `AudioUtilTTS.toml` and the ones mods bring in `presets\*.toml`. A preset on the default model needs the default voice installed; `autts player` shows the voice in use.

## Settings

All under `[player]` in `AudioUtilTTS.toml`; see [Configuration Reference](configuration.md#player).

| Key | Default | Meaning |
|---|---|---|
| `enable` | `true` | `false`: DBReV handles those lines as before, in silence |
| `preset` | `"bella"` | The voice. Empty = `female` or `male` by your character's sex |
| `max_wait` | `5.0` | The longest the conversation waits for a line, in seconds, 1 to 30 |

## Checking on it

| Command | Does |
|---|---|
| `autts player` | Whether your lines are spoken, DBReV's state, the voice, and this session's counts |
| `autts player 0` | Switch it off for this session (`1` = on) |

`AudioUtilTTS.log` has a line for every line DBReV offers: `generating, about N s`, or `declined` and why. DBReV's own log, `DBReV.log` with its Debug toggle on, shows the same lines from its side. If nothing is offered at all, check that a voice pack is selected in DBReV's menu, and that `autts player` does not say `no DBReV` or `not registered`.
