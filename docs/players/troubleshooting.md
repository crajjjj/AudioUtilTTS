# Troubleshooting & Logs

`autts status` in the console and the plugin's log answer most questions.

!!! tip "Quick links"
    - Nothing is spoken at all: [What `autts status` says](#what-autts-status-says)
    - A mod's line was not spoken: [A line was not spoken](#a-line-was-not-spoken)
    - An NPC stays silent although voices are installed: [An NPC stays silent](#an-npc-stays-silent)
    - The mouth does not move: [The mouth stays still](#the-mouth-stays-still)
    - No AudioUtilTTS pages in SKSE Menu Framework: [The in-game menu is not there](#the-in-game-menu-is-not-there)

## Where the logs are

In `Documents\My Games\Skyrim Special Edition\SKSE\`, also under Mod Organizer 2:

| File | What it tells you |
|---|---|
| `AudioUtilTTS.log` | The settings and models as loaded, one `Spoke ...` line per line spoken (with its generation time), every dropped line and every unvoiced dialogue line with the reason. Missing: the DLL never loaded |
| `AudioUtil.log` | Playback problems of narration and character speech |
| `skse64.log` | Why a plugin was rejected, usually a wrong game version or a missing Address Library |

## What `autts status` says

- **`ready | model ...`**: all is well.
- **`ready | no default model (...)`**: the default voice is not installed or could not be loaded. Voices with a model of their own speak; narration and the generic voices do not.
- **`not ready: ...`**: nothing can be spoken:

| Answer | What to do |
|---|---|
| `AudioUtil.dll is not loaded` | Install AudioUtil |
| `disabled in AudioUtilTTS.toml` | Set `[general] enable = true`, then `autts reload` |
| `still starting`, `the model is still loading` | Wait a second and ask again |
| `model folder not found: ...` | No voice model is installed at all. Install one, or fix `[model] dir` |
| `no .onnx model in ...`, `tokens.txt missing in ...`, `voices.bin missing in ...`, `neither espeak-ng-data nor a lexicon in ...`, `... missing in ... (Pocket model)` | The model folder is incomplete: install it again |
| `no voice in ...\voices` | A Pocket model needs its recordings; see [Pocket voices](models.md#pocket-voices) |
| `voice clip voices\...: ...` | That recording cannot be read, and the reason follows. Save it again as a plain wav, or remove it |
| `the speech runtime rejected the model in ...` | Not a model the plugin reads; see [Voice Models](models.md) |
| `no playable narration slot yet (restart the game once)` | Restart the game once |

`switched off` at the start means `autts enable 0` was used; `autts enable 1` turns it back on. An unknown `autts` command means ConsoleUtil Extended or `SKSE\CustomConsole\AudioUtilTTSTest.yaml` is missing.

## A line was not spoken

Look for the line's moment in `AudioUtilTTS.log`:

| The log says | Meaning |
|---|---|
| `... line was dropped: another one is being generated` or `... being spoken` | One line at a time, by design. `from 'ModName'` names the mod. Raise [`max_pending`](configuration.md#max_pending), or set `interrupt = true` |
| `... line was ignored: the default model is not loaded` | The line asked for the default voice, which is not installed. See [The default voice is optional](models.md#the-default-voice-is-optional) |
| `... line was ignored: its model '...' is not installed, and there is no default model to fall back on` | A preset names a model you do not have, and there is no default voice to use instead |
| `Preset '...' is not defined ...` | Spoken, but in the default voice: install the [preset file](voices.md#preset-files) the mod comes with |
| `Preset file ...: ... Models it names that are not installed: ...` | At game start: a mod's presets need a voice model you do not have |
| `Speaker is gone or not loaded` | The actor left before the line was ready |
| `AudioUtilTTS_Say ignored: it must be sent by the actor who speaks` | A mod sent the character event from something that is not an actor |
| Nothing at all | The mod never asked. `autts event "Some text"` sends the event a mod would |

## Speech starts late

About two seconds a sentence with the default model; each `Spoke ...` line says how long a line took. Raise `[model] threads`, use a faster Piper voice ([Cost](what-to-expect.md#cost)), or raise `[model] max_loaded` when lines wait for voices to load ([How many fit in memory](models.md#how-many-fit-in-memory)).

## An NPC stays silent

Three checks, in this order:

1. **Does the line have a recording?** Then the plugin leaves it alone, even a silent one another mod ships.
2. **Which voice does the NPC get?** Click them in the console and run `autts voice`; their voice type comes first.
    - `model folder '...'` or another voice: found.
    - `preset 'female' by sex` ... `unvoiced dialogue stays silent`: no voice for that voice type. Install a voice archive named after it, or turn `fallback` on.
    - A voice type you have never seen, such as `SomeFollowerVoice`: a character with a voice type of their own. Give it a [voice type patch](voices.md#voice-type-patches).
3. **Was it the first time?** A new line said right away can be late or unvoiced that once; see [When a line is ready](unvoiced-dialogue.md#when-a-line-is-ready).

Still silent? See below.

## A dialogue line stays unvoiced

`autts dialogue` first:

| It says | Meaning |
|---|---|
| `dialogue off in AudioUtilTTS.toml` | `[dialogue] enable = false` |
| `dialogue off: the game hook is not in place` | This game version's code is not what the plugin expects; dialogue is left alone, the rest works |
| `dialogue switched off` | `autts dialogue 0` or `autts enable 0` |
| `dialogue on` | Running: the log gives the reason per line |

| The log says | What to do |
|---|---|
| `no voice is set up for it and [dialogue] fallback is off` | Give the voice type a voice ([Voice Types](voices.md#voice-types)), or turn `fallback` on |
| `its voice is on the default model, which is not loaded` | Install the default voice, or give the voice type a voice of its own |
| `the config says so` | The voice type has an empty line in `[voicetype]` |
| `Line not spoken late, N s after it was said` | It was not ready within `greetings_max_late`, or the speaker moved on. Next time it comes from the cache |
| `Line said before it was ready` | The game reached a line whose voice was still being generated. A line of one response is then spoken late (`... so it is spoken late`); a response of several plays as silence that once. The next time it comes from the cache |
| `Line not spoken late (...): the game never reported who said it` | The game built the line but did not report it as said, so the plugin does not know whom to play it at. The topic info in the message is the line's record. Next time it comes from the cache |
| `every placeholder file is held by a recent line` | Raise `[dialogue] slots` and restart |
| `the model '...' could not be loaded`, `the model folder '...' holds no model` | The voice's model is missing or broken |

Speech that stops before the subtitle ends was cut at `[playback] max_chars`.

## The mouth stays still

- Character speech needs AudioUtil 0.9.26 or newer.
- AudioUtil keeps the mouth still for a gagged actor, a worn tongue item, a dialogue with the player, and a mouth another mod has claimed.
- `[speech] lipsync` and `[dialogue] lipsync` must be on.
- Narration has no speaker, so no mouth moves. For your own character, switch to third person.

## The in-game menu is not there

`AudioUtilTTS.log` says at game start: `Menu: off, SKSE Menu Framework is not installed`, `Menu: off, this SKSE Menu Framework lacks: ...` (update the framework), or `Menu: registered`.

A menu change that seems to do nothing: hover over it. One marked "(after a restart)" waits for the next game start. When a voice pack ships its own `AudioUtilTTS.toml`, the menu edits that copy, and reinstalling the pack puts its values back.

## Speech plays at the wrong pitch or speed

Set `[playback] output_rate = 44100` and run `autts reload`.

## The game closes while a model loads

The speech runtime exits on some malformed model files. Remove the model you added last, and check models with the probe first; see [Tools](../authors/tools.md#the-probe).

## Reporting a problem

Attach `AudioUtilTTS.log` (or `skse64.log` when it is missing), what `autts status` says (and `autts dialogue` or `autts voice` with the NPC selected, for a dialogue problem), the plugin and AudioUtil versions, and the voice models you have. Issues go to the [GitHub issue tracker](https://github.com/crajjjj/AudioUtilTTS/issues).
