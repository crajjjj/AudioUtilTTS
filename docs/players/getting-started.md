# Getting Started

## Requirements

- **Skyrim SE / AE** with **SKSE64** and **Address Library for SKSE Plugins**. Start the game through SKSE.
- **[AudioUtil](https://github.com/crajjjj/AudioUtil) 0.9.29 or newer.**
- **[ConsoleUtil Extended](https://www.nexusmods.com/skyrimspecialedition/mods/133569)** for the `autts` console commands, which is how you check an install.
- **[SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352)**, optional, for the [in-game menu](settings-menu.md).

??? note "With an older AudioUtil"
    The plugin runs with AudioUtil 0.9.9 or newer, with less: before 0.9.26 the mouth stays still on character speech, before 0.9.28 a line that was cut short holds its place until it would have ended, and before 0.9.29 a line spoken late overlaps its speaker's next line.

## What to install

| Archive | What it gives you |
|---|---|
| `AudioUtilTTS-<version>.zip` | The plugin. The only required archive |
| `AudioUtilTTS-Voice-kokoro-en-v0_19.zip` | The default voice model, 11 English voices: narration, and the generic voices for dialogue |
| `AudioUtilTTS-Voice-pocket-tts.zip` | Optional: the [Pocket](models.md#pocket-voices) model, which speaks like a short recording. No voices of its own |
| `AudioUtilTTS-Voices-FillIn.zip` | Optional: [fill-in voices](voices.md#fill-in-voices), one Pocket recording for each of 38 vanilla voice types, used only where nothing else gives a voice. Needs the Pocket model |
| A voice archive named after a voice type, such as `AudioUtilTTS-Voice-MaleNord.zip` | A voice for the NPCs of that voice type |
| A voice type patch | Voices for a mod's characters who have voice types of their own |

The first four are on the [latest release](https://github.com/crajjjj/AudioUtilTTS/releases/latest). Voice archives for the vanilla voice types are shared outside this site, on LoversLab. Install each like any mod; none has a plugin file. An update is the plugin archive only.

## What you hear

| Installed | Dialogue without voice files | Narration and character lines from mods |
|---|---|---|
| The plugin alone | Nothing | Nothing |
| + the default voice | Nothing, unless `[dialogue] fallback = true`: then every such line, in a generic female or male voice | Spoken |
| + voice archives named after voice types | The NPCs of those voice types, each in their own voice | Character lines of those NPCs. Narration needs the default voice |
| + a voice type patch | Also the characters it names | The same |

The rows add up in any combination.

**A voice archive named after a voice type needs no configuration**: `models\MaleNord` speaks for every `MaleNord` NPC. Only lines without a recording are spoken. A Kokoro voice takes about 0.3 GB on disk and 0.6 GB of memory while loaded; see [How many fit in memory](models.md#how-many-fit-in-memory).

The default voice speaks English; Piper voices exist for other languages ([Voice Models](models.md)).

## What gets installed

```
Data\
  SKSE\Plugins\AudioUtilTTS.dll
  SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml          settings, every key commented
  SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.defaults.toml the settings as shipped, for the menu's reset
  SKSE\Plugins\AudioUtilTTS\models\<folder>\           one folder per voice model
  SKSE\Plugins\AudioUtilTTS\voicetypes\                voice type patches
  SKSE\Plugins\AudioUtilTTS\presets\                   preset files that mods bring
  SKSE\Plugins\AudioUtilTTS\cache\                     generated dialogue lines, made while you play
  SKSE\CustomConsole\AudioUtilTTSTest.yaml             the autts console commands
  Scripts\                                             AudioUtilTTS.pex, AudioUtilTTSTest.pex
  Sound\AudioUtilTTS\, Sound\Voice\AudioUtilTTS\       silent placeholder files
```

!!! warning "Leave the placeholder files where they are"
    The silent files under `Sound\` are what generated lines are played through: the game only plays files it saw at launch. Without them nothing is heard. See [How It Works](../authors/how-it-works.md).

## First check

Open the console and run `autts status`. The answer starts with:

- **`ready | model kokoro-en-v0_19 ...`**: the default voice is loaded;
- **`ready | no default model (...)`**: other voices are installed and speak, but narration and the generic voices do not;
- **`not ready:`** and the reason; [Troubleshooting & Logs](troubleshooting.md#what-autts-status-says) lists them.

Then:

```
autts say "The road to Whiterun lay open."
```

The line is spoken about two seconds later. To check an NPC, click them in the console and run:

```
autts voice
autts sayactor "Not so loud."
```

`autts voice` shows their voice type and the voice it gets; `autts sayactor` has them say the line.

## Where to next?

- Delays, dropped lines, memory: [What to Expect](what-to-expect.md).
- Which dialogue lines get a voice, and when: [Unvoiced Dialogue](unvoiced-dialogue.md).
- An NPC has no voice, or the wrong one: [Voices, Presets & Voice Types](voices.md).
- Settings in game: [In-Game Menu](settings-menu.md).
- Something not working: [Troubleshooting & Logs](troubleshooting.md).
