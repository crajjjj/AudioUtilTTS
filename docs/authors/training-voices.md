# Training a Voice

The plugin speaks with any model its runtime reads, so a voice of your own is a model folder like any other. This page describes the route that has produced working voices for this plugin: fine-tuning Kokoro on recordings of one speaker, then exporting the result to the format the plugin loads.

It is a map of what is involved and where it goes wrong, not a turnkey tool. The training code is a separate project, none of it ships with the plugin, and every number below was measured on one machine.

!!! warning "Whose voice"
    Only train on a voice you have the right to use: your own, or one whose speaker agreed to it. Recordings of a voice actor, a game's included, are that person's performance. A model made from them is for your own install, not something to publish, unless the rights holders agree.

## Two routes

| | A fine-tuned Kokoro voice | A Piper voice |
|---|---|---|
| What you get | A Kokoro model holding one voice of your own | A VITS model with one voice |
| Time to generate a line in game | Like the default model: about 2 s a sentence | Several times faster |
| Memory per loaded model | About 625 MB | About 350 MB |
| State | Done for this plugin. The steps are below | Downloaded Piper voices load today. Training one of your own has not been tried here |

The rest of this page is the first route. The second is at [the end](#a-piper-voice-instead).

## What you need

- **Recordings of one speaker, each with its exact text.** Clips of 1 to 15 seconds, one sentence or so each, without music or other voices. The voices trained so far used between 600 and 3,300 clips each; the smallest sets worked.
- **An NVIDIA GPU.** A 16 GB card was used. The training recipe asks for 10 GB or more; at a batch size of 2, stage one peaked at 9.2 GB.
- **Linux.** The recipe targets Linux and macOS. Ubuntu under WSL2 on Windows was used, with one catch described [below](#gpu-memory-under-wsl).
- **About an hour to an hour and a half per voice** on an RTX 5080 with the schedule below.
- **The recipe:** [kikiri-tts](https://github.com/semidark/kikiri-tts) (Apache 2.0), a fine-tuning recipe for Kokoro-82M built on a patched StyleTTS2. Its `docs/TRAINING_GUIDE.md` is the reference for everything this page only names.

The recipe is written for German. For English the only real change is the phonemizer; see step 2.

## The steps

### 1. Set up

Clone the recipe with its submodules and create its Python environment (`uv sync`), plus a second environment for training, as its guide asks. Build the `monotonic_align` extension, and download the Kokoro base weights (`kokoro-v1_0.pth` from `hexgrad/Kokoro-82M`) that the fine-tuning starts from.

### 2. Build the dataset

The trainer wants two list files, one line per clip:

```
<file name>|<phonemes>|<speaker number>
```

- **Phonemes, not text.** Turn each clip's text into Kokoro's phonemes with [misaki](https://github.com/hexgrad/misaki)'s English front end (`en.G2P`), the one Kokoro's own English voices were made with. Drop any character outside Kokoro's vocabulary.
- **Filter.** Leave out clips shorter than 1 second or longer than 15, and lines longer than 510 phoneme tokens.
- **Hold some back.** About 5% of the lines go into the validation list, the rest into the training list.
- **The speaker field is a number**, `0` for a single voice.
- **Out-of-domain text.** The trainer also reads a file of phonemized sentences that are not in the dataset, a few thousand unrelated lines.

### 3. Train stage one

`train_first.py`. This trains the acoustic parts and the style encoder, which Kokoro's published weights do not include. It is where the likeness to the speaker is bought.

### 4. Train stage two

`train_second.py`, starting from the `first_stage.pth` that stage one leaves in its run folder. This trains how long, how high and how loud each sound is.

### 5. Extract the voice

`scripts/extract_voicepack.py` runs the clips through the trained style encoders and writes the voice itself, a small tensor file:

```
python scripts/extract_voicepack.py --model <stage-two checkpoint> --style-encoder-model <first_stage.pth> --audio-dir <the clips> --output voice.pt
```

### 6. Export for the plugin

The plugin needs a folder with four things (see [What a model folder holds](voice-models.md#what-a-model-folder-holds)):

| File | Where it comes from |
|---|---|
| `model.onnx` | Your checkpoint, exported to ONNX with the metadata the runtime reads |
| `voices.bin` | Your voice from step 5, as raw 32-bit floats |
| `tokens.txt` | Unchanged from the stock `kokoro-multi-lang-v1_0` package on the [sherpa-onnx model list](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models) |
| `espeak-ng-data\` | Unchanged from the same package |

The export follows sherpa-onnx's own `scripts/kokoro/v1.0`. The folder comes to about 330 MB, nearly all of it `model.onnx`.

!!! danger "Write `version = 1` into the metadata"
    The runtime treats a Kokoro model marked `version 2` as a multi-language build that must be given a lexicon or a language. The plugin passes those for the default model only, and the runtime does not report the missing setting: it exits, which closes the game. A model marked `version 1` is loaded the way the default model is, with espeak-ng alone, and speaks the same.

??? example "The export script"
    Run from the recipe's folder, in its own environment with `onnx` installed: `python export.py <stage-two checkpoint> <voice.pt> <output folder> <voice name>`. Then copy `tokens.txt` and `espeak-ng-data` in beside the two files it writes.

    ```python
    import json
    import sys
    from pathlib import Path

    import onnx
    import torch

    sys.path.insert(0, "scripts")
    from test_inference import convert_checkpoint

    from kokoro import KModel
    from kokoro.model import KModelForONNX

    checkpoint, voicepack, out_dir, speaker = sys.argv[1:5]
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)

    weights = convert_checkpoint(checkpoint, str(out / "_kmodel.pth"))
    with open("training/config.json") as f:
        config = json.load(f)

    with torch.no_grad():
        model = KModel(repo_id="not-used", model=weights, config=config, disable_complex=True).to("cpu").eval()
        tokens = torch.LongTensor([[0, *torch.randint(1, 100, (48,)).tolist(), 0]])
        style = torch.rand(1, 256, dtype=torch.float32)
        speed = torch.rand(1)
        torch.onnx.export(
            KModelForONNX(model),
            (tokens, style, speed),
            str(out / "model.onnx"),
            input_names=["tokens", "style", "speed"],
            output_names=["audio"],
            dynamic_axes={"tokens": {1: "sequence_length"}, "audio": {0: "audio_length"}},
            opset_version=14,
        )
    (out / "_kmodel.pth").unlink(missing_ok=True)

    voice = torch.load(voicepack, map_location="cpu", weights_only=True).numpy()
    assert voice.shape == (510, 1, 256) and str(voice.dtype) == "float32", (voice.shape, voice.dtype)
    (out / "voices.bin").write_bytes(voice.tobytes())

    graph = onnx.load(str(out / "model.onnx"))
    meta = {
        "model_type": "kokoro",
        "language": "English",
        "has_espeak": 1,
        "sample_rate": 24000,
        "version": 1,  # not 2: see the note above
        "voice": "en-us",
        "style_dim": ",".join(map(str, voice.shape)),
        "n_speakers": 1,
        "id2speaker": f"0->{speaker}",
        "speaker2id": f"{speaker}->0",
        "speaker_names": speaker,
    }
    while len(graph.metadata_props):
        graph.metadata_props.pop()
    for key, value in meta.items():
        prop = graph.metadata_props.add()
        prop.key = key
        prop.value = str(value)
    onnx.save(graph, str(out / "model.onnx"))
    ```

    The voices made so far were exported with `version` 2 and then rewritten to 1 in a second step; the script above writes 1 directly, which gives the same file.

### 7. Check it, then install it

Speak a line with the [probe](tools.md#the-probe) before the model goes anywhere near a load order:

```
xmake run probe <the exported folder> "Some text" 0 out.wav
```

Then put the folder under `Data\SKSE\Plugins\AudioUtilTTS\models\`, and either name it after the voice type it is for or give it a `model.toml`. Both are described in [Shipping a Voice Model](voice-models.md).

## The schedule that worked

Count training in steps, not epochs: an epoch is as long as the dataset, so the same number of epochs is a very different amount of training for 600 clips and for 3,000. With a batch size of 2 an epoch is half as many steps as there are clips.

| | Steps | For 1,200 clips |
|---|---|---|
| Stage one | about 4,400 | 8 epochs |
| Stage two | about 2,200 | 4 epochs |
| Joint phase | none | `joint_epoch` set past the last epoch |

What the measurements behind it said, on the first voice:

- **Stage one buys the likeness.** How close the generated voice sits to the real recordings kept rising with more of stage one, but slowly after the first few thousand steps.
- **Stage two is done early by the numbers.** One epoch of it scored about the same as twenty-six. To the ear, a little more of it made the voice livelier, which is why the table has more than one epoch.
- **The joint phase changed nothing measurable** and its epochs take longer, so it is left out.

## Where it goes wrong

### GPU memory under WSL

Under WSL the NVIDIA driver lets an allocation larger than the card's memory succeed by spilling into system RAM. Training does not fail: it carries on many times slower (14 seconds a step instead of half a second), with the GPU shown as fully busy. Cap PyTorch below the card's size before training starts, so that a batch that does not fit raises an error instead:

```python
torch.cuda.set_per_process_memory_fraction(0.90)
```

On a 16 GB card, batch sizes of 4 and 6 did not fit. A batch size of 2 does.

### Stage two runs eight times too slow

`train_second.py` switches on PyTorch's anomaly detection near its top (`torch.autograd.set_detect_anomaly(True)`). That records a stack trace for every operation. Set it to `False`.

### Stage two saves nothing at the end

Stage two writes a checkpoint only when the epoch number divides by `save_freq`, and nothing when it finishes. With 30 epochs and `save_freq` 5 the last four epochs are never saved. Choose `save_freq` so that the last epoch is one of the saved ones.

### Stage two cannot be continued

A stage-two checkpoint names its weights differently from a stage-one checkpoint, and the recipe's loader does not insist on a match. Resuming stage two from a stage-two checkpoint therefore loads nothing, reports no error, and trains an empty model: the result is unusable. To get a longer stage two, run it again from `first_stage.pth`.

### Several voices in one model

Tried, on a short schedule, with ten speakers: every voice came out as the same voice. The recipe trains the style encoders from scratch. One voice barely depends on them; telling ten apart does, and the short schedule was not enough for that. One model per voice works, at the price of one model in memory per voice (see [How many fit in memory](../players/models.md#how-many-fit-in-memory)).

### The voice comes out higher than the speaker

Every voice trained this way speaks higher than its recordings, by 4% to 38% across the voices measured, and more of stage two raises it further. They were kept as trained.

### A different phonemizer at run time

The runtime turns English text into phonemes with espeak-ng, whatever the model, while the recipe trains on misaki's phonemes. The two spell some sounds differently. Measured on held-out lines with a speech recognizer, it made no difference that matters: 7.1% of words misheard on the training phonemes, 7.7% through the plugin's runtime, and 7.4% for the original recordings. A retrain on espeak-ng phonemes is not needed.

### Setup gaps

At the recipe's state in mid 2026, its package list lacked `pandas`, and `monotonic_align` had to be installed with pip: the copy built in place is hidden by its own checkout folder.

## In the game

- **It costs what the default model costs.** A fine-tuned Kokoro voice is the same size and speed as `kokoro-en-v0_19`: about two seconds a sentence, and about 625 MB once loaded. With one model per voice type, `[model] max_loaded` decides how many stay in memory.
- **Replacing a model needs no cleanup.** After copying new files over a model, run `autts reload`. Dialogue lines are cached per model file, so the old voice's lines are not played again.
- **Hear it on an NPC.** Click one whose voice type the model speaks for, then run `autts voice` and `autts sayactor "Some text"`.

## A Piper voice instead

[Piper](https://github.com/OHF-Voice/piper1-gpl) voices are VITS models, which the plugin loads today, and they generate a line several times faster than Kokoro. Piper has its own training tools.

None of that has been tried here with a self-trained voice. What is known is what a model folder has to hold to load: an `.onnx` model carrying the metadata the runtime reads, a `tokens.txt`, and `espeak-ng-data`. The Piper voices on the [sherpa-onnx model list](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models) went through a conversion to get there, and a voice of your own would need the same. Check the result with the probe before anything else.
