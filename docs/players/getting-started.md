# Getting Started

## Requirements

- **Skyrim SE / AE** with **SKSE64** and **Address Library for SKSE Plugins**. Start the game through the SKSE loader (or the SKSE entry in your mod manager), not the vanilla launcher.
- **[AudioUtil](https://github.com/crajjjj/AudioUtil) 0.9.9 or newer.** It plays narration and character speech. Use 0.9.26 or newer for the mouth to move on character speech, and 0.9.28 or newer for a line that was cut short to make room for the next one at once. Dialogue is played by the game itself and does not need it.
- **[ConsoleUtil Extended](https://www.nexusmods.com/skyrimspecialedition/mods/133569)**, only for the `autts` console commands.

## Installing

Take both archives from the [latest release](https://github.com/crajjjj/AudioUtilTTS/releases/latest) and install them like any mod:

- `AudioUtilTTS-<version>.zip`: the plugin, its config, scripts and console commands.
- `AudioUtilTTS-Voice-kokoro-en-v0_19.zip`: the default voice model. It is a separate download because it is large and does not change with the plugin.

Without a voice model the plugin loads but stays silent; `autts status` says why.

!!! tip "Updating"
    A new plugin version is the small archive only. The voice archive stays as it is unless the release notes say otherwise.

## What gets installed

```
Data\
  SKSE\Plugins\AudioUtilTTS.dll
  SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml         settings, every key commented
  SKSE\Plugins\AudioUtilTTS\licenses\                 third-party notices
  SKSE\Plugins\AudioUtilTTS\models\kokoro-en-v0_19\   the voice model (second archive)
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

A working install answers with a line that starts `ready | model kokoro-en-v0_19 (11 voices, 24000 Hz)`. Anything else starts with `not ready:` and names the reason; [Troubleshooting & Logs](troubleshooting.md) lists them.

Then make it talk:

```
autts say "The road to Whiterun lay open."
```

The console answers `queued` and the line is spoken about two seconds later. To hear a character, click an NPC in the console first:

```
autts voice
autts sayactor "Not so loud."
```

`autts voice` shows the NPC's voice type and the voice it leads to. `autts sayactor` makes them say the line, positioned at them, with their mouth moving.

## Where to next?

- Curious about delays, dropped lines and memory? Read [What to Expect](what-to-expect.md).
- Want mod dialogue without voice files spoken? Read [Unvoiced Dialogue](unvoiced-dialogue.md).
- Want a different voice, or one voice per kind of NPC? Read [Voices, Presets & Voice Types](voices.md).
- Something not working? See [Troubleshooting & Logs](troubleshooting.md).
