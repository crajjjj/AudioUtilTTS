#!/usr/bin/env python3
"""A trained checkpoint and its voice -> the two files only this voice has, in
the format AudioUtilTTS loads (a sherpa-onnx Kokoro model): model.onnx with its
metadata, and voices.bin. tokens.txt and espeak-ng-data are copied in by the
caller.

Follows sherpa-onnx's own scripts/kokoro/v1.0 (export_onnx.py, add_meta_data.py,
generate_voices_bin.py).

Usage, from the recipe's folder and in its environment:
    export.py <stage-two checkpoint> <voice.pt> <output folder> <voice name>
"""
import json
import sys
from pathlib import Path

import onnx
import torch

sys.path.insert(0, "scripts")
from test_inference import convert_checkpoint  # noqa: E402

from kokoro import KModel  # noqa: E402
from kokoro.model import KModelForONNX  # noqa: E402

if len(sys.argv) != 5:
    sys.exit(__doc__)
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
    # 1, not 2: the runtime takes a "version 2" Kokoro model for a multi-language
    # build that must be given a lexicon or a language, and it exits the process
    # when it gets neither. The plugin passes those for its default model only,
    # so a version 2 model named by a preset or a voice type would close the
    # game. Version 1 is loaded like the default model, with espeak-ng alone.
    "version": 1,
    "voice": "en-us",
    "style_dim": ",".join(map(str, voice.shape)),
    "n_speakers": 1,
    "id2speaker": f"0->{speaker}",
    "speaker2id": f"{speaker}->0",
    "speaker_names": speaker,
    "comment": "Kokoro-82M v1.0 fine-tuned on one voice",
}
while len(graph.metadata_props):
    graph.metadata_props.pop()
for key, value in meta.items():
    prop = graph.metadata_props.add()
    prop.key = key
    prop.value = str(value)
onnx.save(graph, str(out / "model.onnx"))
print(f"model.onnx {(out / 'model.onnx').stat().st_size / 2**20:.0f} MB, voices.bin "
      f"{(out / 'voices.bin').stat().st_size / 1024:.0f} KB, voice 0 = {speaker}")
