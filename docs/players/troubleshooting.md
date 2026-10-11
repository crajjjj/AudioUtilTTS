# Troubleshooting & Logs

Two things answer most questions: `autts status` in the console, and the plugin's log.

!!! tip "Quick links"
    - Nothing is spoken at all? Start with [What `autts status` says](#what-autts-status-says).
    - Some lines are skipped? See [A line was not spoken](#a-line-was-not-spoken).
    - Mod dialogue stays silent? See [A dialogue line stays unvoiced](#a-dialogue-line-stays-unvoiced).
    - The mouth does not move? See [The mouth stays still](#the-mouth-stays-still).

## Where the logs are

Logs live under your **My Games** folder: `Documents\My Games\Skyrim Special Edition\SKSE\`.

| Log | File | What it tells you |
|---|---|---|
| **AudioUtilTTS** | `AudioUtilTTS.log` | The settings as read, every model loaded, one `Spoke ...` line per spoken line with how long it took to generate, every dropped line with the reason, and why a dialogue line stayed unvoiced. If this file is **missing**, the DLL never loaded |
| **AudioUtil** | `AudioUtil.log` | Playback problems of narration and character speech |
| **SKSE** | `skse64.log` | Why a plugin was rejected, usually a wrong game version or a missing Address Library. Search it for `AudioUtilTTS` |

!!! note "Mod Organizer 2 users"
    Logs go to the real `Documents\My Games\...` folder, not into MO2's virtual file system or the Overwrite folder.

## What `autts status` says

A working install answers with a line that starts `ready`. Otherwise:

| Answer | Meaning | What to do |
|---|---|---|
| `not ready: AudioUtil.dll is not loaded` | AudioUtil is missing or failed to load | Install AudioUtil 0.9.9 or newer |
| `not ready: disabled in AudioUtilTTS.toml` | `[general] enable = false` | Set it to `true`, then `autts reload` |
| `not ready: still starting` or `the model is still loading` | The model loads in the background at game start | Wait a second and ask again |
| `not ready: model folder not found: ...` | The voice model is not installed, or `[model] dir` names a folder that is not there | Install the voice archive, or fix the name |
| `not ready: no .onnx model in ...`, `tokens.txt missing in ...`, `voices.bin missing in ...`, `neither espeak-ng-data nor a lexicon in ...` | The model folder is incomplete | Install the model again |
| `not ready: the speech runtime rejected the model in ...` | The files are not a model the runtime reads | Use a supported model; see [Voice Models](models.md) |
| `not ready: no playable narration slot yet (restart the game once)` | The placeholder files were created this session | Restart the game. The game only plays files it saw at launch |
| An answer that starts with `switched off` | Speech was switched off with `autts enable 0` | `autts enable 1` |

The command `autts` itself is unknown? ConsoleUtil Extended is missing, or `SKSE\CustomConsole\AudioUtilTTSTest.yaml` was not installed.

## A line was not spoken

Open `AudioUtilTTS.log` and look for the line's moment.

- **`A N-char narration line was dropped: another one is waiting or being generated`** or **`... another one is being spoken, N s to go`**. This is by design: one line at a time. Raise [`[playback] max_pending`](configuration.md#max_pending) to let lines wait, or set `interrupt = true` to let a new line cut in.
- **`Speaker is gone or not loaded, a N-char line was dropped`**. The actor left before the line was ready.
- **`Preset '...' is not defined in AudioUtilTTS.toml, using the default voice`**. The line was spoken, but not in the voice the mod asked for. Add the preset; see [Presets](voices.md#presets).
- **`AudioUtilTTS_Say ignored: it must be sent by the actor who speaks`**. A mod sent the character event from something that is not an actor.
- **`No playable ... slot`**. Restart the game once; see the table above.
- **Nothing in the log at all.** The mod never asked. Check it with `autts event "Some text"`, which sends the same event a mod would.

## Speech starts late

A line is generated when it is asked for: about two seconds per sentence with the default model. Each `Spoke ...` line in the log says how long that line took.

- Raise `[model] threads` to get a line sooner at the cost of more cores for that moment.
- A Piper voice generates several times faster than Kokoro; see [Cost](what-to-expect.md#cost).
- The first line of a model that was not in memory is later by about a second. If lines keep being delayed with several models in use, raise `[model] max_loaded`; see [How many fit in memory](models.md#how-many-fit-in-memory).

## A dialogue line stays unvoiced

First run `autts dialogue`. It says whether the feature is on, and counts the lines given a voice, left unvoiced and generated this session.

| It says | Meaning |
|---|---|
| `dialogue off in AudioUtilTTS.toml` | `[dialogue] enable = false`. A change needs a game restart |
| `dialogue off: the game hook is not in place` | The game code was not what the plugin expects, so it patched nothing. The log line starts `Dialogue: the game code is not what this version expects`. Everything else still works |
| `dialogue switched off` | Switched off with `autts dialogue 0` or `autts enable 0` |
| `dialogue on` | Running. Read on |

With the feature on, the log gives the reason per line, or once for a reason that holds line after line:

- **`no voice is set up for it and [dialogue] fallback is off`**. The speaker's voice type has no voice. Click the NPC and run `autts voice`, then set one up ([Voice Types](voices.md#voice-types)) or turn `fallback` on.
- **`the config says so`**. The voice type has an empty line in `[voicetype]`.
- **The line was unvoiced the first time only.** That is by design for a line said right away; see [When a line is ready](unvoiced-dialogue.md#when-a-line-is-ready).
- **`every placeholder file is held by a recent line`**. Raise `[dialogue] slots` and restart.
- **`the model '...' could not be loaded`** or **`the model folder '...' holds no model`**. The model the voice type points at is missing or broken.
- **The line has a voice file already.** A silent or placeholder voice file from another mod counts as voiced, and the plugin leaves the line alone.
- **The line has nothing to say.** `...` and stage directions are skipped; `autts dialogue` counts them as "with nothing to say".

## The mouth stays still

- **Character speech** needs AudioUtil 0.9.26 or newer. With an older one the log says `Character lines play without lipsync`, and `autts status` shows `lipsync off (AudioUtil older than 0.9.26)`.
- AudioUtil keeps the mouth still for a gagged actor, an actor wearing a tongue item, an actor in a dialogue with the player, and a mouth another mod has claimed.
- `[speech] lipsync` and `[dialogue] lipsync` must be `true`.
- Narration never moves a mouth: it has no speaker.
- On your own character, switch to third person to see it.

## Speech plays at the wrong pitch or speed

Set `[playback] output_rate = 44100` and run `autts reload`.

## The game closes while a model loads

The speech runtime exits on some malformed model files, and takes the game with it. Remove the model you added last, and check a model with the probe before using it in game; see [Tools](../authors/tools.md#the-probe).

## Reporting a problem

Attach:

1. `SKSE\AudioUtilTTS.log` (or `SKSE\skse64.log` if it is missing).
2. What `autts status` answers, and `autts dialogue` for a dialogue problem.
3. The plugin version, the AudioUtil version, and which voice models are installed.

A clear "what I did, what happened, what I expected" plus the log is usually enough to find the cause. Issues go to the [GitHub issue tracker](https://github.com/crajjjj/AudioUtilTTS/issues).
