#!/usr/bin/env python3
"""Pack generated lines (wav + lip from `probe --batch`) into the voice files a
legacy DBVO pack holds: Sound\\DBVO\\<pack>\\<line>.fuz, the same format as the
recorded packs (xWMA 44.1 kHz mono 48 kbit/s + the mouth animation).

Usage: pack_fuz.py <jobs.tsv> <wav dir> <pack voice folder> <xwmaencode.exe> [workers] [gain dB]
A line that already has its .fuz is left alone, so the run resumes.

Gain: the generated voice is quieter than a recorded pack, so each line can be
raised by a fixed amount (never past full scale). With no gain argument the
number in <wav dir>\\_gain_db.txt is used, when that file exists.
"""
import struct
import subprocess
import sys
import tempfile
import wave
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

jobs_path, wav_dir, dest, encoder = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4]
workers = int(sys.argv[5]) if len(sys.argv) > 5 else 8
if len(sys.argv) > 6:
    gain_db = float(sys.argv[6])
elif (wav_dir / "_gain_db.txt").exists():
    gain_db = float((wav_dir / "_gain_db.txt").read_text().strip())
else:
    gain_db = 0.0
GAIN = 10 ** (gain_db / 20)
dest.mkdir(parents=True, exist_ok=True)
jobs = [l.rstrip("\n").split("\t") for l in open(jobs_path, encoding="utf-8")]
tmp = Path(tempfile.mkdtemp(prefix="xwm_"))


def louder(wav, out):
    """A copy of the wav raised by GAIN, held just under full scale."""
    with wave.open(str(wav), "rb") as w:
        params = w.getparams()
        samples = np.frombuffer(w.readframes(params.nframes), dtype=np.int16).astype(np.float32)
    peak = float(np.abs(samples).max()) if len(samples) else 0.0
    gain = min(GAIN, 31800.0 / peak) if peak > 0 else 1.0
    with wave.open(str(out), "wb") as w:
        w.setparams(params)
        w.writeframes(np.clip(samples * gain, -32767, 32767).astype(np.int16).tobytes())


def pack(job):
    ident, name = job[0], job[1]
    wav, lip, fuz = wav_dir / f"{ident}.wav", wav_dir / f"{ident}.lip", dest / f"{name}.fuz"
    if fuz.exists():
        return "kept"
    if not wav.exists():
        return "not spoken yet"
    xwm = tmp / f"{ident}.xwm"
    source = wav
    try:
        if GAIN != 1.0:
            source = tmp / f"{ident}.wav"
            louder(wav, source)
        r = subprocess.run([encoder, str(source), str(xwm)], capture_output=True, text=True)
        if r.returncode != 0 or not xwm.is_file() or xwm.stat().st_size < 64:
            return "encoder failed"
        lip_bytes = lip.read_bytes() if lip.exists() else b""
        part = fuz.with_name(fuz.name + ".part")
        # FUZE container: magic, u32 version (1), u32 lip size, LIP block, audio
        part.write_bytes(b"FUZE" + struct.pack("<II", 1, len(lip_bytes)) + lip_bytes + xwm.read_bytes())
        part.replace(fuz)
        return "packed"
    except OSError as e:
        return f"error: {type(e).__name__}"
    finally:
        xwm.unlink(missing_ok=True)
        if source != wav:
            source.unlink(missing_ok=True)


with ThreadPoolExecutor(workers) as pool:
    results = list(pool.map(pack, jobs))
try:
    tmp.rmdir()
except OSError:
    pass
counts = {}
for r in results:
    counts[r] = counts.get(r, 0) + 1
size = sum(f.stat().st_size for f in dest.glob("*.fuz"))
print(f"{counts} | gain {gain_db:+.1f} dB | {len(list(dest.glob('*.fuz')))} files, {size / 2**20:.0f} MB in {dest}")
