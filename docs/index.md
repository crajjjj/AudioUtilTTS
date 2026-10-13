# AudioUtilTTS

Live text-to-speech for Skyrim SE. It gives a voice to lines that have none:

- **Dialogue without voice files.** Plenty of mod dialogue ships as text only. The plugin speaks those lines while you play. With a voice installed for the speaker's voice type, they sound like that kind of NPC.
- **Lines a mod hands it.** A mod sends a line of text and it is spoken a moment later, as narration or by a character, through [AudioUtil](https://github.com/crajjjj/AudioUtil).

The voice model runs inside the plugin, on the CPU, with no internet connection and no helper program. Speech is generated on background threads, not on the game's own.

!!! info "What a voice type is"
    Every NPC in Skyrim has a voice type, such as `MaleNord`, `FemaleEvenToned` or `MaleGuard`. It decides whose recordings the game plays for them, and it is what this plugin picks a voice by. A voice installed for `MaleNord` speaks for every NPC of that voice type.

## What it speaks

| | What it is | What it needs |
|---|---|---|
| **Unvoiced dialogue** | Game dialogue that has no voice file, spoken in the voice of the speaker's voice type | A voice for that voice type. Nothing has to be called or patched |
| **Narration** | A line with no speaker, played flat | A mod that sends it, with one mod event or one Papyrus call |
| **Character speech** | A line said by an actor: positioned at them, with their mouth moving | A mod, the same way |

These docs are split into two tracks. Pick the one that fits you:

## For Players

Install it, hear it, set it up. Start here if you just want to use the mod.

- [Getting Started](players/getting-started.md): what to install, what each archive gives you, and a first check in the console
- [What to Expect](players/what-to-expect.md): how soon a line is spoken, what happens to lines that arrive too fast, and what it costs in memory and CPU
- [Unvoiced Dialogue](players/unvoiced-dialogue.md): how dialogue without voice files gets a voice, what happens to greetings, and the cache
- [Voices, Presets & Voice Types](players/voices.md): which NPC gets which voice, voice type patches, and naming voices of your own
- [Voice Models](players/models.md): adding voices, when a model needs a file of its own, and how many fit in memory
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
- [Shipping a Voice Model](authors/voice-models.md): what a model folder holds, how it names the voice types it speaks for, and the ready-made lines it can bring
- [How It Works](authors/how-it-works.md): placeholder files, the dialogue patch, and the statically linked speech runtime
- [Tools](authors/tools.md): the offline probe, the voice training kit and the voice pack gap filler
- [Building from Source](authors/building.md): xmake, the fetched dependencies, and the repo layout

---

The changelog lives on the [GitHub releases page](https://github.com/crajjjj/AudioUtilTTS/releases).
