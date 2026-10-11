# Voices, Presets & Voice Types

Three layers decide who speaks a line:

- A **voice** is a number inside a model.
- A **preset** gives a model, a voice number, a speed and a volume one name that mods ask for.
- A **voice type** is the game's own label on every NPC. Mapping voice types to voices is what makes an NPC's generated lines sound like that NPC.

All of it lives in `Data\SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml`. After editing the file, run `autts reload` in the console.

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

Try one with `autts sayas "Not so loud." whisper`.

## Voice types

Every NPC has a voice type, and that is what an unvoiced dialogue line is spoken for. The same lookup gives a character their voice for character speech when the mod names no preset.

A voice type finds its voice in this order:

1. its line in `[voicetype]`, naming a preset
2. a preset named exactly like the voice type
3. a model that lists the voice type in its own `model.toml` (see [Models that bring their own voice types](models.md#models-that-bring-their-own-voice-types))
4. a model folder named exactly like the voice type (its first voice)
5. the preset `female` or `male` by sex, only with `[dialogue] fallback = true`

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

!!! info "Added in 0.2.1"
    Step 3, a model's own `model.toml`, is new in 0.2.1. In 0.2.0 the order was steps 1, 2, 4 and 5.

### Finding out who gets which voice

Click an NPC in the console, then:

```
autts voice
autts sayactor "Some text"
```

`autts voice` shows their voice type and the voice it leads to, with the reason: `preset 'malenord'`, `listed by model ...`, `model folder ...`, `preset 'female' by sex`, or `never spoken for`. When the voice is only the one by sex and `fallback` is off, it adds that unvoiced dialogue stays silent for this NPC. `autts sayactor` lets you hear the voice. With nothing selected, both act on the player.

### What happens with no voice set up

| | Unvoiced dialogue | Character speech from a mod |
|---|---|---|
| `[dialogue] fallback = false` (default) | The voice type stays unvoiced | The preset `female` or `male` by sex, else the default voice |
| `[dialogue] fallback = true` | The preset `female` or `male` by sex, else the default voice | The same |

The setting only governs dialogue. A character line a mod asks for is spoken either way, unless the voice type is silenced with an empty `[voicetype]` line and the mod named no preset.
