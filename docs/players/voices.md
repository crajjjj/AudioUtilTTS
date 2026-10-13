# Voices, Presets & Voice Types

## The short version

Every NPC has a **voice type**: the game's own label for whose recordings they use, such as `MaleNord`, `FemaleEvenToned` or `MaleGuard`. The plugin picks a voice by that label. Click an NPC in the console and run `autts voice` to see their voice type and the voice it leads to.

| You have | What to do |
|---|---|
| A voice archive named after a voice type | Install it. `models\MaleNord` speaks for `MaleNord`, with nothing to configure |
| A character with a voice type of their own, such as a follower from a mod | Install or write a [voice type patch](#voice-type-patches) that lends them an installed voice |
| Voice types with no voice, and any voice is better than none | Set `[dialogue] fallback = true`: they get the preset `female` or `male`, which needs the default voice |
| A voice type that should never be spoken for | Add `Name = ""` under [`[voicetype]`](#voice-types) |
| Your own idea of who sounds like what | [Presets](#presets) and [`[voicetype]`](#voice-types) |

The rest of this page is the detail behind that table.

## The three layers

- A **voice** is a number inside a model.
- A **preset** gives a model, a voice number, a speed and a volume one name that mods ask for.
- A **voice type** is the game's own label on every NPC. Mapping voice types to voices is what makes an NPC's generated lines sound like that NPC.

Presets and your own voice type lines live in `Data\SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml`. After editing the file, run `autts reload` in the console.

## Voices

The default model, `kokoro-en-v0_19`, has 11 English voices:

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

Set the default with `voice = N` under `[voice]`, or give a preset its own `voice = N`. To hear one in game, run `autts sayv "Some text" 4` with any number from the table.

Other models number their voices differently; most Piper voices have just one, number 0.

## Presets

A preset is a named voice:

```toml
[preset.whisper]
voice = 2

[preset.fast]
model = "vits-piper-en_US-lessac-medium"
speed = 1.1
```

- **Anything a preset leaves out comes from the defaults** in `[model]` and `[voice]`. A preset on another model that names no voice uses that model's first one.
- **The shipped config defines four**: `narrator`, `female`, `male` and `whisper`, all on the default model, so they cost no extra memory.
- **Names ignore case.** Rename, change or add presets freely.
- **An unknown name falls back to the default voice**, so a mod can ask for a role without knowing what you have installed. The log notes the unknown name once.
- **A preset may name a different model.** That model is loaded the first time a line asks for it, which delays that one line by about a second, and then stays in memory. See [Voice Models](models.md#how-many-fit-in-memory).
- **A mod can bring presets of its own**, as a [preset file](#preset-files). Yours win over them.

Try one with `autts sayas "Not so loud." whisper`.

### Preset files

Added in 0.3.2. There is nothing to set up: a mod that needs voices of its own installs a preset file, and the plugin reads it on top of your config. `AudioUtilTTS.toml` stays yours.

Such a file is `Data\SKSE\Plugins\AudioUtilTTS\presets\<any name>.toml` and looks like the presets in your config:

```toml
[preset.mymod_stern]
voice = 9
speed = 0.9

[preset.mymod_witch]
model = "my-voices"
voice = 3
```

- **Your own config wins.** A preset that `AudioUtilTTS.toml` defines keeps its definition, whatever a file says. To change a mod's preset, copy it into `AudioUtilTTS.toml` under the same name and edit it there. A preset is taken whole: one of yours that sets only `speed` gets its model and voice from the defaults, not from the mod's file.
- **Files add up.** Every `.toml` file in that folder is read, in the order of their names. A name that two files define goes to the later one, and `AudioUtilTTS.log` names both. The exception: when the later one's model is not installed and the earlier one's is, the earlier one stays.
- **A missing model does not break the mod.** A line a mod sends through such a preset is spoken with the default voice instead, at the preset's own speed and volume. Without the default voice installed there is nothing to fall back on, and the line is not spoken.
- **The log says what was read.** At game start `AudioUtilTTS.log` has one line per file: how many presets it added, which names were not taken because your config defines them, and which models it names that are not installed.
- **A preset from a file is for lines that ask for it by name.** It never speaks for a voice type just because it is named like one; that is what a [voice type patch](#voice-type-patches) is for. A [`[voicetype]`](#voice-types) line of your own can name it.
- **Read at game start and on `autts reload`.**

## Voice types

Every NPC has a voice type, and that is what an unvoiced dialogue line is spoken for. The same lookup gives a character their voice for character speech when the mod names no preset.

A voice type finds its voice in this order:

1. its line in `[voicetype]`, naming a preset
2. a preset of `AudioUtilTTS.toml` named exactly like the voice type
3. a voice type patch that names an installed model (see [Voice type patches](#voice-type-patches))
4. a model that lists the voice type in its own `model.toml` (see [Models that bring their own voice types](models.md#models-that-bring-their-own-voice-types))
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
- **An empty preset name silences a voice type.** `CrDogVoice = ""` means that voice type is never spoken for, whatever else is installed: not in dialogue, and not in character speech that names no preset.
- **The quickest setup is no config at all.** A model folder called `models\MaleNord` speaks for the voice type `MaleNord`.
- **A preset whose model is not installed is passed over** in steps 1 and 2, with a note in the log, and the lookup goes on with the next step.

!!! info "Added in 0.2.1 and 0.3.0, changed in 0.3.2"
    A model's own `model.toml` (step 4) is new in 0.2.1, voice type patches (step 3) in 0.3.0. In 0.2.0 the order was steps 1, 2, 5 and 6. Up to 0.3.1 a preset whose model is not installed ended the lookup: unvoiced dialogue of that voice type stayed silent.

### Voice type patches

Added in 0.3.0. A patch gives voice types to voices that are already installed. It is one small file of its own, so the characters of a mod can be given voices by dropping a file in, with no line in `AudioUtilTTS.toml` and nothing added to a model's folder.

**What it is for.** A mod that brings its own recordings usually gives each of its characters a voice type of their own. Those recordings stay as they are. But other mods add dialogue for every NPC, and for such a character those lines have no recording and no voice either: no voice archive is named after a voice type that exists in one mod only. A patch lends the character an installed voice, usually the one of the stock voice type closest to them.

`Data\SKSE\Plugins\AudioUtilTTS\voicetypes\<any name>.toml`:

```toml
# voice type editor ID = the model folder that speaks for it (its first voice)
[voicetype]
HjoromirVoice = "MaleYoungEager"
GorrVoice = "MaleBrute"

# when the model holds several voices, or the voice needs its own speed or volume
RumarinVoice = { model = "my-voices", voice = 3, speed = 1.0, volume = 1.0 }
```

- **Patches add up.** Every `.toml` file in that folder is read, in the order of their names. A voice type that two patches name goes to the later one, and `AudioUtilTTS.log` names both. The exception: when the later one's voice is not installed and the earlier one's is, the earlier one stays.
- **A missing voice is no error.** An entry whose model is not installed is skipped: the lookup goes on as if the patch did not name that voice type. At game start `AudioUtilTTS.log` has one line per patch: how many voice types it names, and which of the voices it asks for are not installed.
- **Your own config wins.** `[voicetype]` and the presets in `AudioUtilTTS.toml` are asked first, so you can overrule a patch or silence a voice type with `Name = ""`.
- **Read at game start and on `autts reload`.**
- **Recorded lines stay as they are.** A patch only matters for the lines of that character that have no recording.

#### Writing one yourself

1. Click the character in the console and run `autts voice`. The answer names their voice type first, for example `HjoromirVoice`.
2. Pick an installed voice for them: the name of a folder under `models\`, such as `MaleYoungEager`.
3. Put the line into a file of your own, `Data\SKSE\Plugins\AudioUtilTTS\voicetypes\MyPatch.toml`:

    ```toml
    [voicetype]
    HjoromirVoice = "MaleYoungEager"
    ```

4. Run `autts reload`, then `autts voice` again. It now answers with `model 'maleyoungeager' by the patch MyPatch.toml`.

!!! note "Mod Organizer 2"
    A file added to a mod folder while the game runs is not seen by the game. Create the file before starting the game; after that, edits to it are picked up by `autts reload`.

### Finding out who gets which voice

Click an NPC in the console, then:

```
autts voice
autts sayactor "Some text"
```

`autts voice` shows their voice type and the voice it leads to, with the reason: `preset 'malenord'` (followed by `of <file>` for a preset from a preset file), `model '...' by the patch ...`, `listed by model ...`, `model folder ...`, `preset 'female' by sex`, or `never spoken for`. When the voice is only the one by sex and `fallback` is off, it adds that unvoiced dialogue stays silent for this NPC. When the voice cannot speak, it says why: `not spoken: the default model is not loaded` for a voice on the default model without that one installed, `its model is not installed` or `its model could not be loaded` for a voice whose own model is the trouble. `autts sayactor` lets you hear the voice. With nothing selected, both act on the player.

### What happens with no voice set up

| | Unvoiced dialogue | Character speech from a mod |
|---|---|---|
| `[dialogue] fallback = false` (default) | The voice type stays unvoiced | The preset `female` or `male` by sex, else the default voice |
| `[dialogue] fallback = true` | The preset `female` or `male` by sex, else the default voice | The same |

The setting only governs dialogue. A character line a mod asks for is spoken either way, unless the voice type is silenced with an empty `[voicetype]` line and the mod named no preset.

The presets `female` and `male` and the default voice are all on the default model. Without that one installed, both columns of the table read "not spoken": only a voice with a model of its own speaks.
