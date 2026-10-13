# Tools

Three tools live in the repo beside the plugin. All work outside the game.

| Tool | Folder | For |
|---|---|---|
| [The probe](#the-probe) | `tools/probe` | Checking a model and hearing a voice without starting the game |
| [The voice training kit](#the-voice-training-kit) | `tools/voicetrain` | Making a voice of your own from recordings |
| [The voice pack gap filler](#the-voice-pack-gap-filler) | `tools/dbvo` | Generating the player lines a recorded voice pack lacks |

## The probe

`probe.exe` is a small console program that runs the same speech core as the plugin. Use it to check a model before it goes into a load order, to hear a voice, and to time a line on your machine.

Build it once (see [Building from Source](building.md)):

```
xmake build probe
```

### Speak one line

```
xmake run probe <model dir> "text" [voice] [out.wav] [threads] [output rate]
```

```
xmake run probe dist\SKSE\Plugins\AudioUtilTTS\models\kokoro-en-v0_19 "Some text" 7 out.wav
```

It prints how long the line took to generate and writes `out.lip` beside the wav: the mouth animation a dialogue line with that audio would get. AudioUtil's LipSim plays the pair.

`voice` defaults to 0, `out.wav` to `probe.wav`, `threads` to 2.

### Speak a whole list

```
probe --batch <model dir> <list file> <out dir> [voice] [threads] [output rate] [lip strength]
```

Speaks a list with the model loaded once. The list is UTF-8, one line each: a file name, a tab, the text. It writes `<out dir>\<file name>.wav` and `.lip`. A line whose wav is already there is skipped, so a stopped run resumes.

### Other modes

| Command | Does |
|---|---|
| `probe --text <file>` | Reads the file's bytes as a line of game text and prints, in hex and as text, what the speech runtime would be handed. For checking how encoded text is cleaned up |
| `probe --rest out.lip` | Writes the closed-mouth placeholder lip alone |

!!! tip "Why check a model here first"
    The speech runtime exits on some malformed model files. In the probe that closes a console window; in the game it closes the game.

## The voice training kit

Scripts in [`tools/voicetrain`](https://github.com/crajjjj/AudioUtilTTS/tree/main/tools/voicetrain). They fine-tune Kokoro on recordings of one speaker and export a model folder the plugin loads, in four commands: `setup` once, then `prepare`, `train` and `export` for each voice.

```powershell
.\voice.ps1 setup
.\voice.ps1 make -Name MyVoice -Clips D:\clips\myvoice -Out D:\voices
```

The training runs under Linux with an NVIDIA card; on Windows the kit drives WSL 2 for you. [Training a Voice](training-voices.md) is the guide.

## The voice pack gap filler

Scripts in [`tools/dbvo`](https://github.com/crajjjj/AudioUtilTTS/tree/main/tools/dbvo). Dragonborn Voice Over plays a recorded file for each player line its voice pack has, and stays silent for the rest. These scripts find the lines of a load order that no installed pack covers and generate a voice file for each, in the pack's own layout and format, so that mod plays them like any recorded line.

Made for legacy packs (`Sound\DBVO\<pack>\<line>.fuz`) under Mod Organizer 2.

### What you need

- `probe.exe` and a model folder, as above
- Python 3 with `lz4` and `numpy`
- `xwmaencode.exe` from the Creation Kit (`Tools\Audio` in the game folder)

### Steps

```
python dbvo_gaps.py  <MO2 instance> <profile> <game Data> <pack id> out
python prep_lines.py out\all_lines.tsv out\pack_names.txt out
python generate.py   out\jobs.tsv <probe.exe> <model dir> <voice> wav "<new mod folder>\Sound\DBVO\<pack id>" <xwmaencode.exe> 3 5
```

1. `dbvo_gaps.py` reads the dialogue topics of every active plugin and lists the player's lines (`all_lines.tsv`) and the files the enabled packs hold (`pack_names.txt`). `summary.txt` says how much is covered, per plugin.
2. `prep_lines.py` writes the work list: a line's file name, and what is spoken for it. A direction such as `(Persuade)` or `[Attack]` is not spoken, and a line that is nothing else is skipped. Lines with text the game fills in at run time (`<Alias=...>`) cannot be made ahead.
3. `generate.py` speaks the list on several processes (here 3, with 5 threads each), then packs each line: xWMA at 44.1 kHz mono 48 kbit/s plus the mouth animation, as recorded packs are. It can be stopped and started again.

Put the result in a mod folder of its own and enable it. It only holds file names no other pack has, so its place in the mod order does not matter.

### Notes

- **Voice.** Pick the voice nearest the pack's.
- **Loudness.** The generated voice is quieter than a recorded pack. Write the difference in dB into `wav\_gain_db.txt` before the packing step and each line is raised by that much.
- **After changing the mod list**, run the three steps again: only the new lines are generated.

The [README in that folder](https://github.com/crajjjj/AudioUtilTTS/blob/main/tools/dbvo/README.md) has the details, including the file name rule.
