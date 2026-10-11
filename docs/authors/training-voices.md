# Training a Voice

The plugin speaks with any model its runtime reads, so a voice of your own is a model folder like any other. The repository carries a training kit that makes one from recordings of a single speaker: it fine-tunes Kokoro and exports the result in the format the plugin loads.

```powershell
.\voice.ps1 setup
.\voice.ps1 make -Name MyVoice -Clips D:\clips\myvoice -Out D:\voices
```

The first line is run once. The second takes a folder of clips and, an hour or two later, leaves `D:\voices\MyVoice`: a folder to copy into the plugin's `models` folder.

!!! warning "Whose voice"
    Only train on a voice you have the right to use: your own, or one whose speaker agreed to it. Recordings of a voice actor, a game's included, are that person's performance. A model made from them is for your own install, not something to publish, unless the rights holders agree.

## What you need

| | |
|---|---|
| **A PC with an NVIDIA card** | 16 GB of video memory was used. Stage one of the training peaked at 9.2 GB, so a 12 GB card may do; less will not |
| **Windows with WSL 2**, or Linux | The training runs under Linux. On Windows that is WSL 2, which the kit drives for you |
| **About 20 GB of disk space** | The tools take 13 GB inside WSL. Each voice keeps 3 to 4 GB of training files there until you delete its folder |
| **Recordings of one speaker** | A few hundred clips or more, each with its exact words. See [The clips](#3-the-clips) |
| **Time** | Setup is a large download. A voice takes an hour to an hour and a half on an RTX 5080 |

The kit has been run on Windows 11 with Ubuntu under WSL 2 and an RTX 5080.

## 1. Get WSL and the kit

If WSL is not installed yet, open PowerShell as administrator and run:

```powershell
wsl --install -d Ubuntu
```

Restart Windows, start **Ubuntu** once from the Start menu and pick a user name and password. Then check that Linux sees the card: `wsl nvidia-smi` should list it. If it does not, update the NVIDIA driver in Windows.

The kit is the folder [`tools/voicetrain`](https://github.com/crajjjj/AudioUtilTTS/tree/main/tools/voicetrain) of the repository. Download the repository (**Code**, then **Download ZIP**) or the `AudioUtilTTS-VoiceTraining` archive of a release that has one, and unpack it anywhere.

## 2. Set up, once

Open PowerShell in the kit's folder:

```powershell
.\voice.ps1 setup
```

It installs what the training needs inside WSL, eight steps in all: system tools, the training recipe, two Python environments, the Kokoro base weights. Nothing is installed in Windows. Expect several GB of downloads; the step that takes longest says so. It may ask for your Linux password once, to install the system tools.

It ends with a check that prints your card and `Setup done`. Running it again is harmless: finished steps are skipped.

??? note "PowerShell refuses to run the script"
    Windows blocks downloaded scripts by default. Start it this way instead:

    ```powershell
    powershell -ExecutionPolicy Bypass -File .\voice.ps1 setup
    ```

## 3. The clips

Put the recordings of one speaker into a folder, with the text of each clip.

- **Audio:** wav, mp3, flac, ogg, m4a, opus or wma. Any sample rate; the kit converts.
- **Length:** 1.5 to 12 seconds each. Shorter and longer clips are left out, so cut long recordings into sentences first.
- **Clean:** one voice, no music, no other speakers, little room echo.
- **How many:** the voices made so far used 600 to 3,300 clips, and the smallest sets worked. Under 100 usable clips the kit refuses.
- **The text must be exact.** The words of each clip as spoken, in normal spelling. A wrong transcript teaches the voice wrong sounds.

The text comes in one of two forms.

=== "One file for all"

    A file `metadata.csv` in the folder, one line per clip: the file name, a `|`, the text.

    ```
    line001.wav|The road is long, but the weather should hold until evening.
    line002.wav|Be careful out there.
    ```

=== "One file per clip"

    A `.txt` file with the same name beside each clip.

    ```
    line001.wav
    line001.txt
    line002.wav
    line002.txt
    ```

Only English is supported: the text is turned into Kokoro's American English phonemes.

## 4. Make the voice

```powershell
.\voice.ps1 make -Name MyVoice -Clips D:\clips\myvoice -Out D:\voices
```

`-Name` is the voice's name and the name of the model folder: letters, digits, `-` and `_`. `-Out` is any folder of yours; the result is written into it.

`make` runs three steps in a row. Each can also be run on its own, which is the way to go the first time:

| Step | Command | Does | Takes |
|---|---|---|---|
| Prepare | `.\voice.ps1 prepare -Name MyVoice -Clips D:\clips\myvoice` | Converts the clips, turns the text into phonemes, and reports what it kept and what it left out, with the reason | Minutes |
| Train | `.\voice.ps1 train -Name MyVoice` | Stage one, then stage two. Shows a progress line every hundred steps | An hour or more |
| Export | `.\voice.ps1 export -Name MyVoice -Out D:\voices` | Builds the model folder, loads it the way the plugin does and speaks a few test sentences | Minutes |

Read what `prepare` reports before you train: if half the clips were left out as too long, an hour of training is the wrong moment to find out.

While a training runs, `.\voice.ps1 status -Name MyVoice` in a second window shows where it is. Keep the first window open until it says `Trained`.

### Naming the voice types

A voice can say which of the game's voice types it speaks for, so that installing it is all it takes:

```powershell
.\voice.ps1 export -Name MyVoice -Out D:\voices -VoiceTypes MaleNord,MaleGuard
```

That writes a `model.toml` into the folder. Exporting again is quick and needs no new training. A folder named exactly like a voice type speaks for that voice type with no file at all. Both are described in [Shipping a Voice Model](voice-models.md).

## 5. Listen, then install

The export leaves two folders:

```
D:\voices\MyVoice\            the model: model.onnx, voices.bin, tokens.txt, espeak-ng-data
D:\voices\MyVoice-samples\    six test sentences spoken with it
```

Listen to the samples first. They were made by the same speech runtime the plugin uses, loading the folder the same way, so a voice that speaks here loads in the game.

To install, copy the folder `MyVoice` into `Data\SKSE\Plugins\AudioUtilTTS\models` (in your mod manager, into a mod of its own). Then, in game:

```
autts reload
autts voice
autts sayactor "Some text"
```

with an NPC of one of the voice's voice types selected in the console. To use the voice by name instead, give it a [preset](../players/voices.md#presets) with `model = "MyVoice"`.

## Settings

The defaults are the schedule the voices so far were made with. Change them only with a reason.

| `voice.ps1` | `voice.sh` | Default | Meaning |
|---|---|---|---|
| `-Steps1` | `STEPS1` | 4400 | Training steps of stage one. This is where the likeness is bought |
| `-Steps2` | `STEPS2` | 2200 | Training steps of stage two |
| `-Fresh` | `FRESH=1` | off | Train stage one again although it is done |
| `-Distro` | | the default one | Which WSL distribution to use |
| `-Set BATCH=2` | `BATCH` | 2 | Batch size. 4 and 6 did not fit a 16 GB card |
| `-Set GPU_FRACTION=0.8` | `GPU_FRACTION` | 0.90 | Share of the card's memory PyTorch may use |
| `-Set MIN_SECONDS=1`, `MAX_SECONDS=15` | `MIN_SECONDS`, `MAX_SECONDS` | 1.5, 12 | Which clip lengths are used |
| `-Set VOICETRAIN_HOME=...` | `VOICETRAIN_HOME` | `~/audioutiltts-voicetrain` | Where the kit keeps its tools and voices inside Linux |

`-Set` passes any setting of `voice.sh` through, several at once separated by commas.

On Linux the same commands are `bash voice.sh setup`, `bash voice.sh make MyVoice /path/to/clips /path/to/out`, and so on, with the settings as environment variables. `bash voice.sh help` lists them.

## When something goes wrong

| What you see | Meaning | What to do |
|---|---|---|
| `WSL is not installed` | | See [step 1](#1-get-wsl-and-the-kit) |
| `NO GPU visible to PyTorch` at the end of setup | Linux does not see the card | Update the NVIDIA driver in Windows, check `wsl nvidia-smi`, run setup again |
| `no text found for the clips` | No `metadata.csv` and no `.txt` files | See [The clips](#3-the-clips) |
| `only N usable lines` | Too few clips survived the filters | Read the "left out" line above it. Usually the clips are too long: cut them |
| `another training is running on this machine` | Two trainings do not fit one card | Wait for the first |
| `stage one did not finish` with `out of memory` in the lines above | The card is too small for the batch, or something else is using it | Close games and other GPU programs. There is no smaller batch than 2 |
| Training crawls, with the GPU shown as busy | Memory spilled into system RAM | Lower `GPU_FRACTION`, and close other GPU programs |
| `the exported model did not load or speak` | The runtime refused the folder | Do not install it. The lines above name the reason |
| The window was closed mid-training | | Run `train` again. A finished stage one is kept; an unfinished stage starts over |

Every step writes its full output into the voice's folder inside Linux, `~/audioutiltts-voicetrain/voices/<name>/`: `prepare.txt`, `stage1.log`, `stage2.log`, `export.log`. From Windows that folder is reachable in Explorer under `\\wsl$`. Deleting it removes the voice's training files; the exported model folder is not touched.

## What the result is like

- **It costs what the default model costs.** A fine-tuned Kokoro voice is the same size and speed as `kokoro-en-v0_19`: about 330 MB on disk, about two seconds a sentence, and about 625 MB of memory once loaded.
- **One voice, one model.** With a model per voice type, `[model] max_loaded` decides how many stay in memory; see [How many fit in memory](../players/models.md#how-many-fit-in-memory).
- **It speaks a little higher than the speaker.** Every voice made this way came out higher than its recordings, by 4% to 38% across the voices measured. They were kept as trained.
- **Replacing a model needs no cleanup.** After copying new files over a model, run `autts reload`. Dialogue lines are cached per model file, so the old voice's lines are not played again.

## Under the hood

The training itself is [kikiri-tts](https://github.com/semidark/kikiri-tts) (Apache 2.0), a fine-tuning recipe for Kokoro-82M built on a patched StyleTTS2. `setup` fetches it at a fixed commit; the kit adds the English front end, the schedule, the export and the checks. What each step runs:

| Step | What happens |
|---|---|
| Prepare | ffmpeg converts each clip to 24 kHz mono. [misaki](https://github.com/hexgrad/misaki)'s English front end, the one Kokoro's own English voices were made with, turns the text into phonemes. About 5% of the lines, 24 at most, are held back for validation |
| Stage one | The recipe's `train_first.py`: the acoustic parts and the style encoder, which Kokoro's published weights do not include |
| Stage two | `train_second.py`, from the `first_stage.pth` stage one leaves: how long, how high and how loud each sound is |
| Export | `extract_voicepack.py` turns the clips into the voice itself. The checkpoint is exported to ONNX as sherpa-onnx's own `scripts/kokoro/v1.0` does, and `tokens.txt` and `espeak-ng-data` come unchanged from the stock `kokoro-multi-lang-v1_0` package |

### The schedule

Training is counted in steps, not epochs: an epoch is as long as the dataset, so the same number of epochs is a very different amount of training for 600 clips and for 3,000. The kit works out the epochs from your clip count.

What the measurements behind the defaults said, on the first voice:

- **Stage one buys the likeness.** How close the generated voice sits to the real recordings kept rising with more of stage one, but slowly after the first few thousand steps.
- **Stage two is done early by the numbers.** One epoch of it scored about the same as twenty-six. To the ear, a little more of it made the voice livelier, which is why the default is more than one epoch's worth.
- **The joint phase changed nothing measurable** and its epochs take longer, so the kit leaves it out.

### What the kit takes care of

These are the places where doing it by hand went wrong.

- **GPU memory under WSL.** The NVIDIA driver lets an allocation larger than the card succeed by spilling into system RAM. Training does not fail: it carries on many times slower. The kit caps PyTorch below the card's size, so a batch that does not fit raises an error instead.
- **Stage two ran eight times too slow.** The recipe's `train_second.py` switches on PyTorch's anomaly detection. `setup` switches it off.
- **Stage two saves nothing at its end**, only when the epoch number divides by a setting. The kit sets that so the last epoch is saved.
- **Stage two cannot be continued** from one of its own checkpoints: the loader takes nothing from it, reports no error, and trains an empty model. The kit always starts stage two from the end of stage one.
- **The model's metadata.** The runtime takes a Kokoro model marked `version 2` for a multi-language build that needs a lexicon or a language. The plugin passes those for the default model only, and the runtime does not report the missing setting: it exits, which closes the game. The kit writes `version 1`, which is loaded the way the default model is.

### Known limits

- **Several voices in one model** do not work on a schedule this short: tried with ten speakers, every voice came out as the same one. The kit makes one model per voice.
- **A different phonemizer at run time.** The runtime turns English into phonemes with espeak-ng, while the training uses misaki's. Measured on held-out lines with a speech recognizer, it made no difference that matters: 7.1% of words misheard on the training phonemes, 7.7% through the plugin's runtime, and 7.4% for the original recordings.
- **English only**, American pronunciation.
- **No transcription and no cutting.** The kit does not write the text of your clips or split long recordings.

## A Piper voice instead

[Piper](https://github.com/OHF-Voice/piper1-gpl) voices are VITS models, which the plugin loads today, and they generate a line several times faster than Kokoro, in about half the memory. Piper has its own training tools.

The kit does not make Piper voices. What a model folder has to hold to load is the same either way: an `.onnx` model carrying the metadata the runtime reads, a `tokens.txt`, and `espeak-ng-data`. The Piper voices on the [sherpa-onnx model list](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models) went through a conversion to get there, and a voice of your own would need the same. Check the result with the [probe](tools.md#the-probe) before anything else.
