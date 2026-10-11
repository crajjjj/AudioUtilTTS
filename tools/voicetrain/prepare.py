#!/usr/bin/env python3
"""Clips and their text -> a training set for one voice.

Usage: prepare.py <clips folder> <voice folder> <recipe folder>

Reads   the audio files in <clips> (wav, mp3, flac, ogg, m4a, opus, wma) and the
        text of each clip, either from <clips>/metadata.csv with one line per
        clip, "file name|text", or from a .txt file of the same name beside
        the clip.
Writes  <voice>/audio/*.wav             24 kHz mono, what the trainer reads
        <voice>/train_list.txt          file|phonemes|0
        <voice>/val_list.txt            the lines held back for validation
        <voice>/texts.csv               file|text
        <voice>/OOD_texts.txt           text the trainer reads beside the dataset
        <voice>/prepare.txt             what was kept and what was left out, and why

Settings, as environment variables: MIN_SECONDS (1.5), MAX_SECONDS (12),
MIN_LINES (100).
"""
import os
import random
import re
import subprocess
import sys
from pathlib import Path

import soundfile as sf

if len(sys.argv) != 4:
    sys.exit(__doc__)
clips, voice, recipe = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])

sys.path.insert(0, str(recipe / "StyleTTS2"))
from kokoro_symbols import dicts  # noqa: E402  the Kokoro phoneme vocabulary

# misaki's English front end is what Kokoro's own English voices were made
# with, so the phonemes match what the base model learned
from misaki import en, espeak  # noqa: E402

MIN_SECONDS = float(os.environ.get("MIN_SECONDS", "1.5"))
MAX_SECONDS = float(os.environ.get("MAX_SECONDS", "12"))
MIN_LINES = int(os.environ.get("MIN_LINES", "100"))
MIN_TOKENS, MAX_TOKENS = 8, 510
AUDIO = {".wav", ".mp3", ".flac", ".ogg", ".m4a", ".opus", ".wma"}

# The trainer picks a random line of "other" text for every clip and insists on
# 50 characters or more, so the file must hold some. These guarantee it.
FILLER = [
    "The road is long, but the weather should hold until evening, and there is an inn before the river.",
    "I have lived in this town my whole life, and I have never seen anything quite like it.",
    "If you are looking for work, speak to the steward up at the keep before the gates close.",
    "Be careful out there. The nights are getting colder, and the wolves come down from the hills.",
    "We left at dawn and followed the old road north until the mountains rose out of the mist.",
    "Nobody has gone into that cave for years, and those who did were in no hurry to talk about it.",
    "She counted the coins twice, wrote the sum into the ledger, and locked the box away again.",
    "There is a market here every week, with traders from as far away as the coast and beyond.",
]


def read_text(path):
    data = path.read_bytes()
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return data.decode("cp1252", errors="replace")


def one_line(text):
    return " ".join(text.replace("|", " ").split())


if not clips.is_dir():
    sys.exit(f"clips folder not found: {clips}")
audio_files = sorted(p for p in clips.iterdir() if p.is_file() and p.suffix.lower() in AUDIO)
if not audio_files:
    sys.exit(f"no audio files in {clips} ({', '.join(sorted(AUDIO))})")

# ---- the text of each clip
texts, unmatched = {}, 0
meta = next((clips / n for n in ("metadata.csv", "metadata.txt") if (clips / n).is_file()), None)
by_stem = {p.stem.lower(): p for p in audio_files}
if meta:
    for number, raw in enumerate(read_text(meta).splitlines()):
        if "|" not in raw:
            continue
        name, text = raw.split("|", 1)
        key = Path(name.strip()).stem.lower()
        if key in by_stem and one_line(text):
            texts[key] = one_line(text)
        elif number > 0 or key not in ("file", "filename", "file name", "name", "id", "clip"):
            unmatched += 1  # text for a clip that is not there (a header line is not counted)
for stem, path in by_stem.items():
    side = path.with_suffix(".txt")
    if stem not in texts and side.is_file() and side != meta:
        text = one_line(read_text(side))
        if text:
            texts[stem] = text
if not texts:
    sys.exit(
        f"no text found for the clips in {clips}.\n"
        'Give each clip its exact words: a metadata.csv with lines "file name|text",\n'
        "or a .txt file with the same name beside each clip."
    )

# ---- audio: 24 kHz mono 16 bit
audio_dir = voice / "audio"
audio_dir.mkdir(parents=True, exist_ok=True)
rows, used, failed = [], set(), 0
dropped = {"no text": 0, "could not be read": 0, "too short": 0, "too long": 0, "nothing to say": 0, "text too long": 0}
print(f"{len(audio_files)} audio files, {len(texts)} with text. Converting...", flush=True)
for i, src in enumerate(audio_files, 1):
    stem = src.stem.lower()
    if stem not in texts:
        dropped["no text"] += 1
        continue
    safe = re.sub(r"[^A-Za-z0-9_.-]", "_", src.stem)
    while safe.lower() in used:
        safe += "_"
    used.add(safe.lower())
    dst = audio_dir / f"{safe}.wav"
    if not dst.is_file() or dst.stat().st_mtime < src.stat().st_mtime:
        done = subprocess.run(
            ["ffmpeg", "-loglevel", "error", "-y", "-i", str(src), "-ac", "1", "-ar", "24000",
             "-sample_fmt", "s16", str(dst)],
            capture_output=True,
        )
        if done.returncode != 0 or not dst.is_file():
            dropped["could not be read"] += 1
            dst.unlink(missing_ok=True)
            continue
    try:
        seconds = sf.info(str(dst)).duration
    except Exception:
        dropped["could not be read"] += 1
        continue
    rows.append((dst.name, texts[stem], seconds))
    if i % 250 == 0:
        print(f"  {i} of {len(audio_files)}", flush=True)
# clips of an earlier run that are gone now must not end up in the voice
keep = {name for name, _t, _s in rows}
for old in audio_dir.glob("*.wav"):
    if old.name not in keep:
        old.unlink()

# ---- phonemes
g2p = en.G2P(trf=False, british=False, fallback=espeak.EspeakFallback(british=False))


def phonemize(text):
    ps, _ = g2p(text)
    return "".join(ch for ch in ps.strip() if ch in dicts)


kept = []
for name, text, seconds in rows:
    if seconds < MIN_SECONDS:
        dropped["too short"] += 1
        continue
    if seconds > MAX_SECONDS:
        dropped["too long"] += 1
        continue
    ps = phonemize(text)
    if len(ps) < MIN_TOKENS:
        dropped["nothing to say"] += 1
        continue
    if len(ps) > MAX_TOKENS:
        dropped["text too long"] += 1
        continue
    kept.append((name, ps, seconds, text))

random.Random(42).shuffle(kept)
n_val = min(24, max(2, len(kept) // 20))
val, train = kept[:n_val], kept[n_val:]


def hours(part):
    return sum(row[2] for row in part) / 3600


report = [
    f"clips folder: {clips}",
    f"audio files: {len(audio_files)}",
    f"kept: {len(kept)} ({hours(kept):.2f} h) = {len(train)} for training + {len(val)} held back",
    f"clips between {MIN_SECONDS:g} and {MAX_SECONDS:g} seconds are used",
    "left out: " + (", ".join(f"{n} {why}" for why, n in dropped.items() if n) or "none"),
]
if unmatched:
    report.append(f"lines of {meta.name} that named no clip: {unmatched}")
if len(train) < MIN_LINES:
    print("\n".join(report))
    sys.exit(
        f"\nonly {len(train)} usable lines, and at least {MIN_LINES} are needed "
        "(a few hundred or more make a good voice)."
    )

for path, part in ((voice / "train_list.txt", train), (voice / "val_list.txt", val)):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for name, ps, _s, _t in part:
            f.write(f"{name}|{ps}|0\n")
with open(voice / "texts.csv", "w", encoding="utf-8", newline="\n") as f:
    for name, _ps, _s, text in kept:
        f.write(f"{name}|{text}\n")

other = [ps for _n, ps, _s, _t in kept if len(ps) >= 60]
random.Random(7).shuffle(other)
other = other[:3000] + [phonemize(text) for text in FILLER]
(voice / "OOD_texts.txt").write_text("\n".join(other) + "\n", encoding="utf-8", newline="\n")

report.append("")
report.append("examples (text, then the phonemes it is trained on):")
for name, ps, _s, text in kept[:3]:
    report += [f"  {text}", f"  {ps}"]
(voice / "prepare.txt").write_text("\n".join(report) + "\n", encoding="utf-8", newline="\n")
print("\n".join(report))
if len(train) < 400:
    print(f"\nnote: {len(train)} lines is on the small side. The voices so far used 600 to 3,300.")
