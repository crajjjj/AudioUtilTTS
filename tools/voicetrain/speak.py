#!/usr/bin/env python3
"""Load an exported model folder the way AudioUtilTTS loads a model that is not
its default one (the same runtime, no lexicon and no language) and speak a few
sentences with it.

A model the runtime cannot read makes it exit, which in the game means the game
closes. Here it only means this script ends without writing samples, so run it
before a model goes anywhere near a load order.

Usage: speak.py <model folder> <output folder>
"""
import sys
import time
from pathlib import Path

import numpy as np
import sherpa_onnx
import soundfile as sf

SENTENCES = [
    "The road is long, but the weather should hold until evening.",
    "I have lived in this town my whole life, and I have never seen anything like it.",
    "If you are looking for work, speak to the steward up at the keep.",
    "Be careful out there. The nights are getting colder.",
    "Thank you. I will not forget this.",
    "What do you want? I am busy.",
]

if len(sys.argv) != 3:
    sys.exit(__doc__)
model, out = Path(sys.argv[1]), Path(sys.argv[2])
for name in ("model.onnx", "voices.bin", "tokens.txt", "espeak-ng-data"):
    if not (model / name).exists():
        sys.exit(f"{name} is missing in {model}")

start = time.time()
tts = sherpa_onnx.OfflineTts(sherpa_onnx.OfflineTtsConfig(
    model=sherpa_onnx.OfflineTtsModelConfig(
        kokoro=sherpa_onnx.OfflineTtsKokoroModelConfig(
            model=str(model / "model.onnx"), voices=str(model / "voices.bin"),
            tokens=str(model / "tokens.txt"), data_dir=str(model / "espeak-ng-data")),
        provider="cpu", num_threads=2),
    max_num_sentences=1, silence_scale=0.2))
print(f"loaded by sherpa-onnx {sherpa_onnx.__version__} in {time.time() - start:.1f} s: "
      f"{tts.num_speakers} voice(s), {tts.sample_rate} Hz")

out.mkdir(parents=True, exist_ok=True)
for i, text in enumerate(SENTENCES, 1):
    start = time.time()
    audio = tts.generate(text, sid=0, speed=1.0)
    samples = np.asarray(audio.samples, dtype=np.float32)
    if len(samples) == 0:
        sys.exit(f"sentence {i} came out empty")
    sf.write(str(out / f"sample_{i}.wav"), samples, audio.sample_rate, subtype="PCM_16")
    print(f"  sample_{i}.wav  {len(samples) / audio.sample_rate:.1f} s of speech, "
          f"generated in {time.time() - start:.1f} s")
(out / "sentences.txt").write_text("\n".join(SENTENCES) + "\n", encoding="utf-8")
