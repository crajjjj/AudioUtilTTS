# Configuration Reference

All settings live in one file, `Data\SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml`, every key commented. Every key is optional; the defaults below are the shipped values. After editing, run `autts reload`. With SKSE Menu Framework, the [In-Game Menu](settings-menu.md) edits the file for you.

A file that does not parse is not used: the previous settings stay, and `AudioUtilTTS.log` names the line.

## [general]

| Key | Default | Meaning |
|---|---|---|
| `enable` | `true` | Master switch. `false` loads no model and speaks nothing |
| `fill_in_voices` | `true` | Use the [fill-in voices](voices.md#fill-in-voices) voice archives offer for voice types nothing else voices. `false` switches them all off |

## [model]

The default model; see [Voice Models](models.md). It is optional: without it only voices with a model of their own speak.

| Key | Default | Meaning |
|---|---|---|
| `dir` | `"kokoro-en-v0_19"` | A folder under `Data\SKSE\Plugins\AudioUtilTTS\models\`. Loaded at game start and kept |
| `family` | `"auto"` | `"auto"`: Pocket when the folder has an `lm_main*.onnx`, Kokoro when it has `voices.bin`, VITS otherwise. Or `"kokoro"`, `"vits"`, `"pocket"` |
| `threads` | `2` | CPU threads per line, 1 to 16. More = ready sooner, and more cores taken from the game for that moment |
| `lexicon` | `""` | Multi-language Kokoro builds only: file names inside the model folder, comma separated |
| `lang` | `""` | Multi-language Kokoro builds only |
| `max_loaded` | `4` | Models kept in memory, the default one included, 1 to 32. See [How many fit in memory](models.md#how-many-fit-in-memory) |

## [voice]

The default voice: what a line gets when it names no preset.

| Key | Default | Meaning |
|---|---|---|
| `voice` | `7` | Voice number inside the default model, or for a Pocket model the name of one of its recordings in quotes. See [Voices](voices.md#voices) |
| `speed` | `1.0` | `1.0` = the model's natural pace, 0.5 to 2.0 |
| `volume` | `1.0` | 0.0 to 1.0, before AudioUtil's group volume |

## [preset.&lt;name&gt;]

A named voice mods ask for. See [Presets](voices.md#presets).

| Key | Default | Meaning |
|---|---|---|
| `model` | the `[model]` one | A model folder, loaded on first use |
| `voice` | the `[voice]` one | Voice number inside that model, or for a Pocket model the name of one of its recordings; on another model, left out = its first voice |
| `speed` | the `[voice]` one | 0.5 to 2.0 |
| `volume` | the `[voice]` one | 0.0 to 1.0 |

The shipped file defines `narrator` (voice 7), `female` (1), `male` (9) and `whisper` (2). Mods can bring more in `presets\*.toml`; yours win over theirs. See [Preset files](voices.md#preset-files).

## [voicetype]

Which preset speaks for a voice type. See [Voice Types](voices.md#voice-types).

```toml
[voicetype]
MaleNord = "malenord"          # voice type editor ID = "preset name"
CrDogVoice = ""                # never spoken for
```

Case is ignored on both sides.

## [dialogue]

Dialogue lines that have no voice file. See [Unvoiced Dialogue](unvoiced-dialogue.md).

| Key | Default | Meaning |
|---|---|---|
| `enable` | `true` | `false` leaves the game unpatched; a change needs a game restart. `autts dialogue 0` switches it off for one session |
| `fallback` | `false` | `true`: a voice type with no voice of its own is spoken with the preset `female` or `male` by its sex (needs the default model) |
| `greetings` | `true` | Speak a new line said right away as soon as it is ready, a moment late, instead of leaving it unvoiced that once. See [Greetings spoken late](unvoiced-dialogue.md#greetings-spoken-late) |
| `greetings_max_late` | `3.0` | The latest such a line may still be spoken, in seconds, 0.5 to 10 |
| `greetings_split` | `true` | Speak a long late line in two parts, so that it starts sooner |
| `prepare_greetings` | `false` | Generate nearby NPCs' greetings ahead of time. Heavy; see [Greetings made ahead](unvoiced-dialogue.md#greetings-made-ahead) |
| `lipsync` | `true` | Move the speaker's mouth with the loudness of the line |
| `lip_strength` | `0.8` | How far the mouth opens, 0.0 to 1.5 |
| `skip_directions` | `true` | Leave out text in `(parentheses)`, `[brackets]` or `*asterisks*` |
| `slots` | `128` | Placeholder voice files, 64 to 256. A line that finds none free stays unvoiced. New ones work after the next restart |
| `cache_max_mb` | `1024` | Size limit of the line cache in MB; the lines used longest ago go first. `0` = no limit |

## [playback]

Narration, plus four keys that reach further: `interrupt` and `max_pending` also govern character speech, `max_chars` and `output_rate` every line, dialogue included.

| Key | Default | Meaning |
|---|---|---|
| `group` | `"narration"` | AudioUtil volume group |
| `channel` | `"narration"` | AudioUtil channel. Must differ from the `[speech]` one |
| `interrupt` | `false` | `true`: a new line cuts into the one being spoken |
| `max_pending` | `0` | Lines that may wait, 0 to 16. See below |
| `max_chars` | `400` | Longer text is cut at the last sentence that fits, 16 to 4000 |
| `slots` | `4` | Placeholder wav files, 1 to 16. New ones work after the next restart |
| `output_rate` | `0` | `0` = the model's own sample rate. Set `44100` if speech plays at the wrong pitch or speed |

### max_pending

- **`0`: one line at a time.** A line that arrives while another is being generated or spoken is dropped and logged. With `interrupt = true` the new one cuts in instead. A line still waiting for the worker is replaced by a newer one.
- **`1` or more**: that many wait and follow each other; past that, the oldest waiting line is dropped.

Narration and character speech each count on their own. A line another mod cut short frees its place at once (AudioUtil 0.9.28 or newer).

## [speech]

Character speech: a line an actor says.

| Key | Default | Meaning |
|---|---|---|
| `group` | `"speech"` | AudioUtil volume group |
| `channel` | `"speech"` | AudioUtil channel. Must differ from the `[playback]` one |
| `lipsync` | `true` | Move the speaker's mouth. Needs AudioUtil 0.9.26 or newer |
| `slots` | `4` | Placeholder wav files, 1 to 16. New ones work after the next restart |

## Files beside it

- `presets\*.toml`: [preset files](voices.md#preset-files) that mods bring.
- `voicetypes\*.toml`: [voice type patches](voices.md#voice-type-patches).
- `models\<folder>\model.toml`: the voice types a model [speaks for](models.md#models-that-bring-their-own-voice-types).
