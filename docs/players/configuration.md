# Configuration Reference

Everything is set in one file:

```
Data\SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml
```

Every key is optional and commented in the file itself. The defaults below are the shipped file. After editing, run `autts reload` in the console; the few keys that need a game restart say so.

With SKSE Menu Framework installed, the [In-Game Menu](settings-menu.md) edits this file for you and applies each change at once.

If the file does not parse, the previous settings are kept and `AudioUtilTTS.log` names the line.

## [general]

| Key | Default | Meaning |
|---|---|---|
| `enable` | `true` | Master switch. `false` also skips loading any model |

## [model]

The default model. It is optional from 0.3.1 on: see [The default voice is optional](models.md#the-default-voice-is-optional) and [Voice Models](models.md).

| Key | Default | Meaning |
|---|---|---|
| `dir` | `"kokoro-en-v0_19"` | A folder under `Data\SKSE\Plugins\AudioUtilTTS\models\`. Loaded at game start and never unloaded. When the folder is not there, only voices with a model of their own speak |
| `family` | `"auto"` | `"auto"` reads the folder: `voices.bin` present = Kokoro, otherwise VITS (Piper voices). Or `"kokoro"` / `"vits"` |
| `threads` | `2` | CPU threads used while a line is being generated, 1 to 16. More threads = the line is ready sooner, but the game loses those cores for that moment |
| `lexicon` | `""` | Only for models that need it (multi-language Kokoro builds). File names inside the model folder, comma separated |
| `lang` | `""` | Only for models that need it |
| `max_loaded` | `4` | How many models may be in memory at once, the default one included, 1 to 32. See [How many fit in memory](models.md#how-many-fit-in-memory) |

## [voice]

The default voice: what a line gets when it names no preset.

| Key | Default | Meaning |
|---|---|---|
| `voice` | `7` | Speaker number inside the default model. See [Voices](voices.md#voices) |
| `speed` | `1.0` | `1.0` = the model's natural pace, 0.5 to 2.0 |
| `volume` | `1.0` | 0.0 to 1.0, before AudioUtil's group volume is applied |

## [preset.&lt;name&gt;]

A named voice a mod asks for by name. See [Presets](voices.md#presets).

| Key | Default | Meaning |
|---|---|---|
| `model` | the `[model]` one | A model folder. Loaded on first use |
| `voice` | the `[voice]` one | Speaker number inside that model. On another model, left out = that model's first voice |
| `speed` | the `[voice]` one | 0.5 to 2.0 |
| `volume` | the `[voice]` one | 0.0 to 1.0 |

The shipped file defines `narrator` (voice 7), `female` (voice 1), `male` (voice 9) and `whisper` (voice 2).

Mods can add presets through files of their own, `Data\SKSE\Plugins\AudioUtilTTS\presets\*.toml`, which hold the same `[preset.<name>]` tables. A preset defined in `AudioUtilTTS.toml` wins over one of the same name from such a file. See [Preset files](voices.md#preset-files).

## [speech]

Character speech: a line spoken by an actor, positioned at them, with their mouth moving.

| Key | Default | Meaning |
|---|---|---|
| `group` | `"speech"` | AudioUtil volume group |
| `channel` | `"speech"` | AudioUtil exclusivity channel. Must differ from the `[playback]` one |
| `lipsync` | `true` | Move the speaker's mouth (AudioUtil lipsync, driven by loudness). Needs AudioUtil 0.9.26 or newer |
| `slots` | `4` | Placeholder wav files for character lines (`Data\Sound\AudioUtilTTS`), 1 to 16. A slot added by raising this starts working after the next restart |

## [dialogue]

Game dialogue that has no recorded voice. See [Unvoiced Dialogue](unvoiced-dialogue.md).

| Key | Default | Meaning |
|---|---|---|
| `enable` | `true` | `false` = the game is not patched at all. **A change needs a game restart**; `autts dialogue 0` switches it off for the session |
| `fallback` | `false` | `false` = a voice type with no voice of its own stays unvoiced. `true` = it is spoken with the preset `female` or `male` by its sex, which needs the default model. Was `true` in 0.2.0 |
| `greetings` | `true` | Speak a new greeting as soon as it is generated, a moment after the NPC said it, instead of leaving it unvoiced the first time. Added in 0.2.1. From 0.3.0 on the same goes for every line of one response that is said right away, such as the line that opens a conversation. See [Greetings spoken late](unvoiced-dialogue.md#greetings-spoken-late) |
| `greetings_max_late` | `3.0` | The longest a line may be late and still be spoken, in seconds, 0.5 to 10. A line that is not ready by then stays unvoiced that once. Added in 0.3.0 |
| `greetings_split` | `true` | Speak a long line that is late in two parts: its first sentence as soon as that is generated, the rest right behind it. Only that first time; the cache gets the whole line. Added in 0.3.0 |
| `prepare_greetings` | `false` | Generate the greetings of nearby NPCs ahead of time, so that a new one is on time. Added in 0.2.1. See [Greetings made ahead](unvoiced-dialogue.md#greetings-made-ahead) |
| `lipsync` | `true` | Move the speaker's mouth, with shapes guessed from the loudness of the line |
| `lip_strength` | `0.8` | How far the mouth opens, 0.0 to 1.5 |
| `skip_directions` | `true` | Leave out stage directions: text in `(parentheses)`, `[brackets]` or `*asterisks*` |
| `slots` | `128` | Placeholder voice files (`Data\Sound\Voice\AudioUtilTTS`), 64 to 256. A line that finds none free stays unvoiced. A slot added by raising this starts working after the next restart |
| `cache_max_mb` | `1024` | Size limit of the cache of generated lines, in MB. The lines used longest ago are dropped first. `0` = no limit |

`wait_ms`, a key of 0.2.0, is gone in 0.2.1: the game is no longer made to wait for a line.

## [voicetype]

Which voice speaks for which of the game's voice types. See [Voice Types](voices.md#voice-types).

```toml
[voicetype]
MaleNord = "malenord"          # voice type editor ID = "preset name"
CrDogVoice = ""                # never spoken for
```

The value is a preset name in quotes. Case is ignored on both sides.

## [playback]

Narration: lines with no speaker. Some keys here reach further: `interrupt` and `max_pending` also govern character speech, and `max_chars` and `output_rate` apply to every line the plugin generates, dialogue included.

| Key | Default | Meaning |
|---|---|---|
| `group` | `"narration"` | AudioUtil volume group |
| `channel` | `"narration"` | AudioUtil exclusivity channel |
| `interrupt` | `false` | `false` = a new line waits for the one being spoken to finish. `true` = a new line cuts in as soon as it is ready |
| `max_pending` | `0` | Lines allowed to wait behind the one being generated, 0 to 16. See below. Was `1` in 0.2.0 |
| `max_chars` | `400` | Longer text is cut at the last sentence that fits, 16 to 4000 |
| `slots` | `4` | Placeholder wav files the speech is written into (`Data\Sound\AudioUtilTTS`), 1 to 16. A slot added by raising this starts working after the next restart |
| `output_rate` | `0` | `0` = write the model's own sample rate (24000 for Kokoro). Set `44100` if speech plays at the wrong pitch or speed |

### max_pending

- **`0`: one line at a time.** A line that arrives while another is being generated or spoken is dropped. A mod that sends lines faster than they can be spoken is then not read out back to back. With `interrupt = true`, a line being spoken does not count: the new one cuts into it. A line that has not been started on yet, because the plugin is busy with a character line or with dialogue, is replaced by a newer one, so the freshest line is the one spoken.
- **`1` or more: that many wait** and follow each other without a pause. When more arrive, the oldest waiting line is dropped.

Narration and character speech are each counted on their own. Every dropped line is noted in `AudioUtilTTS.log` with the reason.

A line that was cut short (AudioUtil's `StopHandle`, `StopChannel` or `StopGroup`, from any mod) frees its lane at once with AudioUtil 0.9.28 or newer. With an older AudioUtil the plugin cannot see the cut, and the lane stays busy until that line would have ended.

## A model's own file

A model folder may carry a `model.toml` naming the voice types it speaks for. It is not part of `AudioUtilTTS.toml`; see [Models that bring their own voice types](models.md#models-that-bring-their-own-voice-types).
