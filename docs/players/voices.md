# Voices, Presets & Voice Types

Every NPC has a **voice type**, such as `MaleNord` or `FemaleEvenToned`: the game's label for whose recordings they use. The plugin picks a voice by it. To see an NPC's voice type and the voice it gets, click them in the console and run `autts voice`.

| You want | Do this |
|---|---|
| NPCs of a voice type to speak | Install a voice archive named after it. `models\MaleNord` speaks for every `MaleNord` NPC, with nothing to configure |
| A mod's character with a voice type of their own to speak | Install or write a [voice type patch](#voice-type-patches) that lends them an installed voice |
| Every other voice type to speak too | Set `[dialogue] fallback = true`: they get a generic female or male voice from the default model |
| A voice type never to speak | Add `Name = ""` under [`[voicetype]`](#voice-types) |
| Your own choice of who sounds like what | [Presets](#presets) and [`[voicetype]`](#voice-types) |

Your own lines go in `Data\SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml`. Run `autts reload` after editing it.

## Voice types

A voice type finds its voice in this order; the first that answers wins:

1. its line in `[voicetype]`, naming a preset
2. a preset of `AudioUtilTTS.toml` named exactly like the voice type
3. a [voice type patch](#voice-type-patches) that names an installed model
4. a model that lists the voice type in its own [`model.toml`](models.md#models-that-bring-their-own-voice-types)
5. a model folder named exactly like the voice type (its first voice)
6. the preset `female` or `male` by sex, only with `[dialogue] fallback = true`

```toml
[voicetype]
MaleNord = "malenord"
FemaleEvenToned = "eventoned"
FemaleYoungEager = "eventoned"   # several voice types can share one voice
CrDogVoice = ""                  # never spoken for

[preset.malenord]
model = "nord-male"        # a model folder with one voice

[preset.eventoned]
model = "my-voices"        # one model holding many voices
voice = 4
```

- **The name is the voice type's editor ID**, as xEdit shows it. Case is ignored.
- **An empty preset name silences a voice type**, in dialogue and in character speech that names no preset.
- **A preset whose model is not installed is passed over** in steps 1 and 2, and the lookup goes on.
- **Character speech uses the same lookup** when the mod names no preset.

### Voice type patches

A mod that brings its own recordings usually gives each of its characters a voice type of their own. Their recorded lines stay as they are, but lines other mods add for them have no recording, and no voice archive is named after a voice type that exists in one mod only. A patch lends such a character an installed voice, usually that of the stock voice type closest to them.

A patch is a file of its own, `Data\SKSE\Plugins\AudioUtilTTS\voicetypes\<any name>.toml`:

```toml
# voice type editor ID = the model folder that speaks for it (its first voice)
[voicetype]
HjoromirVoice = "MaleYoungEager"
GorrVoice = "MaleBrute"

# when the model holds several voices, or the voice needs its own speed or volume
RumarinVoice = { model = "my-voices", voice = 3, speed = 1.0, volume = 1.0 }
```

- **Patches add up.** Every file in that folder is read, in name order. When two name the same voice type, the later one wins, unless its voice is not installed and the earlier one's is.
- **A voice that is not installed is skipped**, and the lookup goes on. `AudioUtilTTS.log` names such voices at game start, one line per patch.
- **Your own config wins**: `[voicetype]` and the presets of `AudioUtilTTS.toml` are asked first.
- **Read at game start and on `autts reload`.** Under Mod Organizer 2, a file added while the game runs is not seen until the next start.

To write one for a character: click them in the console and run `autts voice`; the answer starts with their voice type, for example `HjoromirVoice`. Put a line for it in a file of your own, naming a folder under `models\`:

```toml
[voicetype]
HjoromirVoice = "MaleYoungEager"
```

Run `autts reload`, then `autts voice` again: it now says `model 'maleyoungeager' by the patch MyPatch.toml`.

### Finding out who gets which voice

Click an NPC in the console and run `autts voice`. It shows their voice type, the voice it leads to and why: `preset '...'`, `model '...' by the patch ...`, `listed by model ...`, `model folder ...`, `preset 'female' by sex` or `never spoken for`. It also says when that voice cannot speak, and why. `autts sayactor "Some text"` lets you hear it. With nothing selected, both act on the player.

### What happens with no voice set up

| | Unvoiced dialogue | Character speech from a mod |
|---|---|---|
| `[dialogue] fallback = false` (default) | Stays unvoiced | The preset `female` or `male` by sex, else the default voice |
| `[dialogue] fallback = true` | The preset `female` or `male` by sex, else the default voice | The same |

Both of those voices are on the default model: without it installed, nothing is spoken in either column.

## Presets

A preset is a named voice that mods ask for: a model, a voice number, a speed and a volume.

```toml
[preset.whisper]
voice = 2

[preset.fast]
model = "vits-piper-en_US-lessac-medium"
speed = 1.1
```

- **What a preset leaves out comes from `[model]` and `[voice]`.** A preset on another model that names no voice uses that model's first one.
- **The shipped config has four**: `narrator`, `female`, `male` and `whisper`, all on the default model.
- **An unknown name falls back to the default voice**, so a mod can ask for a role without knowing what you have. The log notes the name once.
- **A preset on another model** loads that model the first time a line asks for it, about a second; see [Voice Models](models.md#how-many-fit-in-memory).
- **Names ignore case.** Rename, change or add presets freely.

Try one with `autts sayas "Not so loud." whisper`.

### Preset files

A mod that needs voices of its own can bring them as a file, `Data\SKSE\Plugins\AudioUtilTTS\presets\<any name>.toml`, holding the same tables:

```toml
[preset.mymod_stern]
voice = 9
speed = 0.9

[preset.mymod_witch]
model = "my-voices"
voice = 3
```

- **Your own config wins.** A preset `AudioUtilTTS.toml` defines keeps its definition. To change a mod's preset, copy all of it into your config under the same name and edit it there.
- **Files add up**, in name order. When two define the same name, the later one wins, unless its model is not installed and the earlier one's is.
- **A missing model does not break the mod.** A line through such a preset is spoken with the default voice, at the preset's speed and volume. Without the default voice it is not spoken. `AudioUtilTTS.log` names the missing models at game start.
- **A preset from a file is for lines that ask for it by name.** It never speaks for a voice type by being named like one; a [voice type patch](#voice-type-patches) does that.
- **Read at game start and on `autts reload`.**

## Voices

A voice is a number inside a model. The default model, `kokoro-en-v0_19`, has 11 English voices:

| Number | Name | Voice |
|---|---|---|
| 0 | af | American female (the model's default blend) |
| 1 | af_bella | American female |
| 2 | af_nicole | American female, whispered |
| 3 | af_sarah | American female |
| 4 | af_sky | American female |
| 5 | am_adam | American male |
| 6 | am_michael | American male |
| 7 | bf_emma | British female (the plugin's default) |
| 8 | bf_isabella | British female |
| 9 | bm_george | British male |
| 10 | bm_lewis | British male |

Set the default with `voice = N` under `[voice]`, or give a preset its own `voice = N`. To hear one, run `autts sayv "Some text" 4`. Most Piper voices have just one voice, number 0. A Pocket model's voices are its recordings; see [Pocket voices](models.md#pocket-voices).
