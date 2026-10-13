# Getting Started

## Requirements

- **Skyrim SE / AE** with **SKSE64** and **Address Library for SKSE Plugins**. Start the game through the SKSE loader (or the SKSE entry in your mod manager), not the vanilla launcher.
- **[AudioUtil](https://github.com/crajjjj/AudioUtil) 0.9.29 or newer.** It plays narration, character speech and the lines that are spoken late.
- **[ConsoleUtil Extended](https://www.nexusmods.com/skyrimspecialedition/mods/133569)** for the `autts` console commands. The plugin speaks without it, but the commands are how you check an install and find out which voice an NPC gets.

??? note "With an older AudioUtil"
    The plugin runs with AudioUtil 0.9.9 or newer, with less:

    - before 0.9.26 the mouth stays still on character speech,
    - before 0.9.28 a line that was cut short keeps its place until it would have ended,
    - before 0.9.29 a line spoken late is not stopped when its speaker goes on to the next one, so the two overlap.

    Dialogue that the game plays itself does not go through AudioUtil.

## What to install

| Archive | What it is | What it is for |
|---|---|---|
| `AudioUtilTTS-<version>.zip` | The plugin, its config, scripts and console commands | Everything |
| `AudioUtilTTS-Voice-kokoro-en-v0_19.zip` | The default voice model: 11 English voices | Narration, lines a mod sends without a voice of its own, and the generic voices for dialogue |
| A voice archive named after a voice type, such as `AudioUtilTTS-Voice-MaleNord.zip` | One voice, made for one voice type | Unvoiced dialogue and character lines of the NPCs of that voice type |
| A voice type patch | One small text file | The characters of a mod who have a voice type of their own |

The first two are on the [latest release](https://github.com/crajjjj/AudioUtilTTS/releases/latest). Install every archive like any mod; none of them has a plugin file to enable.

**Nothing but the plugin is required.** Each of the other archives adds what the table says, and without any voice the plugin loads and stays silent; `autts status` says why.

!!! info "The default voice is optional from 0.3.1 on"
    Voice archives named after voice types work without it. With 0.3.0 and older the default voice has to be installed for dialogue too.

!!! tip "Updating"
    A new plugin version is the small archive only. The voice archives stay as they are unless the release notes say otherwise.

## What you hear

| Installed | Dialogue without voice files | Narration and character lines from mods |
|---|---|---|
| The plugin alone | Nothing | Nothing |
| The plugin and the default voice | Nothing out of the box. With `[dialogue] fallback = true`: every such line, in one generic female or male voice | Spoken, in the default model's voices |
| The plugin and voice archives named after voice types | The NPCs of those voice types, each kind in its own voice. Nothing to configure | Character lines of those NPCs, in their voice. Narration needs the default voice |
| A voice type patch on top | Also the characters the patch names, in the voice it lends them | The same for their character lines |

The rows add up: install the default voice, voice archives and patches in any combination.

### A voice archive named after a voice type

It unpacks to one folder, `Data\SKSE\Plugins\AudioUtilTTS\models\<VoiceType>\`, and the folder name is all the plugin needs: `models\MaleNord` speaks for every NPC whose voice type is `MaleNord`.

- **There is nothing to configure.** No line in `AudioUtilTTS.toml`, no `model.toml` inside the folder, no plugin file.
- **Only lines without a recording are spoken.** A line that has a voice file is never touched.
- **A voice type with no voice stays as it was**, unless you turn `fallback` on.
- **A voice costs disk space and memory.** A Kokoro voice is about 0.3 GB on disk and about 0.6 GB of memory while it is loaded. Up to four voice models are kept loaded at once by default, the default voice included; see [How many fit in memory](models.md#how-many-fit-in-memory).

Characters from mods often have a voice type of their own, which no voice archive is named after. That is what a [voice type patch](voices.md#voice-type-patches) is for.

### Language

The default voice speaks English. Voices for other languages are among the Piper voices; see [Voice Models](models.md).

## What gets installed

```
Data\
  SKSE\Plugins\AudioUtilTTS.dll
  SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml         settings, every key commented
  SKSE\Plugins\AudioUtilTTS\licenses\                 third-party notices
  SKSE\Plugins\AudioUtilTTS\models\kokoro-en-v0_19\   the default voice (its own archive)
  SKSE\Plugins\AudioUtilTTS\models\<VoiceType>\       one folder per voice archive
  SKSE\Plugins\AudioUtilTTS\voicetypes\               voice type patches
  SKSE\CustomConsole\AudioUtilTTSTest.yaml            the autts console commands
  Scripts\AudioUtilTTS.pex  AudioUtilTTSTest.pex      and their sources under Scripts\Source
  Sound\AudioUtilTTS\                                 placeholder wavs for narration and character speech
  Sound\Voice\AudioUtilTTS\                           placeholder wav + lip pairs for dialogue
```

While you play, generated dialogue lines are kept in `Data\SKSE\Plugins\AudioUtilTTS\cache`.

!!! warning "Leave the placeholder files where they are"
    The files under `Sound\AudioUtilTTS` and `Sound\Voice\AudioUtilTTS` are silent on purpose. The game only plays loose files it saw at launch, so the plugin writes each generated line into one of them. Without them nothing can be played. See [How It Works](../authors/how-it-works.md).

## First check

Open the console and run:

```
autts status
```

The answer starts in one of three ways:

- **`ready | model kokoro-en-v0_19 (11 voices, 24000 Hz)`**: the default voice is loaded.
- **`ready | no default model (...)`**: there is no default voice, but other voices are installed. Those speak; narration and the generic voices do not.
- **`not ready:`**: nothing can be spoken, and the rest of the line names the reason. [Troubleshooting & Logs](troubleshooting.md#what-autts-status-says) lists them.

Further along, the line says how many voice models are installed: `1 of 12 models loaded`.

With the default voice installed, make it talk:

```
autts say "The road to Whiterun lay open."
```

The console answers `queued` and the line is spoken about two seconds later.

To check an NPC, click them in the console first:

```
autts voice
autts sayactor "Not so loud."
```

`autts voice` shows the NPC's voice type and the voice it leads to. `autts sayactor` makes them say the line, positioned at them, with their mouth moving.

## Where to next?

- Curious about delays, dropped lines and memory? Read [What to Expect](what-to-expect.md).
- Want to know which dialogue lines get a voice, and when? Read [Unvoiced Dialogue](unvoiced-dialogue.md).
- An NPC has no voice, or the wrong one? Read [Voices, Presets & Voice Types](voices.md).
- Something not working? See [Troubleshooting & Logs](troubleshooting.md).
