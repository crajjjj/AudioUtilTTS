# AudioUtilTTS

Live text-to-speech for Skyrim SE. A mod hands it a line of text and the line is spoken in game a moment later, through [AudioUtil](https://github.com/crajjjj/AudioUtil). It also speaks game dialogue that has no recorded voice, in a voice you choose per voice type.

The voice model runs inside the plugin, on the CPU, with no internet connection and no helper program. Speech is generated on background threads, not on the game's own.

## What it speaks

| | What it is | Who asks for it |
|---|---|---|
| **Narration** | A line with no speaker, played flat | A mod, with one mod event or one Papyrus call |
| **Character speech** | A line said by an actor: positioned at them, with their mouth moving | A mod, the same way |
| **Unvoiced dialogue** | Game dialogue that has no voice file, spoken in the voice of the speaker's voice type | Nobody. The plugin does it by itself |

These docs are split into two tracks. Pick the one that fits you:

## For Players

Install it, hear it, set it up. Start here if you just want to use the mod.

- [Getting Started](players/getting-started.md): requirements, the two archives, and a first check in the console
- [What to Expect](players/what-to-expect.md): how soon a line is spoken, what happens to lines that arrive too fast, and what it costs in memory and CPU
- [Unvoiced Dialogue](players/unvoiced-dialogue.md): how dialogue without voice files gets a voice, what happens to greetings, and the cache
- [Voices, Presets & Voice Types](players/voices.md): the voices of the default model, naming your own, and giving each voice type its voice
- [Voice Models](players/models.md): adding models, models that bring their own voice types, and how many fit in memory
- [Configuration Reference](players/configuration.md): every key of `AudioUtilTTS.toml` in one place
- [Console Commands](players/console-commands.md): the `autts` commands
- [Troubleshooting & Logs](players/troubleshooting.md): what `autts status` and the log say, and what to do about it

## For Mod Authors

Make your mod talk, or release a voice of your own.

- [Overview](authors/overview.md): the ways in, and what to design around
- [Mod Events](authors/modevents.md): speak a line with no dependency on this mod
- [Papyrus API](authors/papyrus-api.md): every function of `AudioUtilTTS.psc`
- [C++ API](authors/cpp-api.md): the same calls for SKSE plugins, with no link dependency
- [Training a Voice](authors/training-voices.md): making a voice of your own from recordings, with the training kit
- [Shipping a Voice Model](authors/voice-models.md): what a model folder holds and how it names the voice types it speaks for
- [How It Works](authors/how-it-works.md): placeholder files, the dialogue patch, and the statically linked speech runtime
- [Tools](authors/tools.md): the offline probe, the voice training kit and the voice pack gap filler
- [Building from Source](authors/building.md): xmake, the fetched dependencies, and the repo layout

---

The changelog lives on the [GitHub releases page](https://github.com/crajjjj/AudioUtilTTS/releases).
