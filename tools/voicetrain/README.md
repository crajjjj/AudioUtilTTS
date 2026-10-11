# Voice training for AudioUtilTTS

Fine-tunes Kokoro on recordings of one speaker and exports a model folder the
plugin loads. Four commands: set up once, then prepare, train and export a
voice. The full guide is the docs page
[Training a Voice](https://crajjjj.github.io/AudioUtilTTS/authors/training-voices/).

Only train on a voice you have the right to use.

## What you need

- Windows with WSL 2 and a Linux distribution (`wsl --install -d Ubuntu`), or
  Linux itself
- An NVIDIA card; 16 GB was used, and stage one peaked at 9.2 GB
- Recordings of one speaker with the exact text of each clip

## From Windows

```powershell
.\voice.ps1 setup
.\voice.ps1 make -Name MyVoice -Clips D:\clips\myvoice -Out D:\voices
```

`make` is `prepare`, `train` and `export` in a row. Each can be run on its own:

```powershell
.\voice.ps1 prepare -Name MyVoice -Clips D:\clips\myvoice
.\voice.ps1 train   -Name MyVoice
.\voice.ps1 export  -Name MyVoice -Out D:\voices -VoiceTypes MaleNord,MaleGuard
.\voice.ps1 status
```

The result is `D:\voices\MyVoice`, to be copied into
`Data\SKSE\Plugins\AudioUtilTTS\models`, and `D:\voices\MyVoice-samples` to
listen to first.

## From Linux

```
bash voice.sh setup
bash voice.sh make MyVoice /path/to/clips /path/to/out MaleNord,MaleGuard
```

`bash voice.sh help` lists the commands and the settings.

## The clips folder

Audio files (wav, mp3, flac, ogg, m4a, opus, wma) of 1.5 to 12 seconds each,
and their text in one of two forms:

- `metadata.csv` in the folder, one line per clip: `file name|text`
- a `.txt` file with the same name beside each clip

## Files

| File | Does |
|---|---|
| `voice.ps1` | Windows front end: turns paths into WSL ones and runs `voice.sh` |
| `voice.sh` | The four commands |
| `prepare.py` | Clips and text to 24 kHz audio, phonemes and the trainer's lists |
| `run_capped.py` | Runs a training script with the GPU memory capped |
| `export.py` | Checkpoint and voice to `model.onnx` and `voices.bin` |
| `speak.py` | Loads an exported folder the way the plugin does and speaks samples |
| `requirements-train.txt` | The training environment, at tested versions |

The training itself is the [kikiri-tts](https://github.com/semidark/kikiri-tts)
recipe (Apache 2.0), fetched by `setup` at a fixed commit. Nothing of it is
included here.
