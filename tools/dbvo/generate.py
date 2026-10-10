#!/usr/bin/env python3
"""Speak every line of the work list that has no voice file yet, on several
processes at once, then pack the results into the pack folder.

Usage: generate.py <jobs.tsv> <probe.exe> <model dir> <voice> <wav dir> <pack voice folder>
                   <xwmaencode.exe> [processes] [threads each]
Safe to stop and start again: lines already packed, or already spoken, are skipped.
The speech processes run at below-normal priority, so the desktop stays usable.
"""
import subprocess
import sys
import time
from pathlib import Path

jobs_path, probe, model, voice, wav_dir, dest, encoder = sys.argv[1:8]
processes = int(sys.argv[8]) if len(sys.argv) > 8 else 4
threads = int(sys.argv[9]) if len(sys.argv) > 9 else 4
wav_dir, dest = Path(wav_dir), Path(dest)
wav_dir.mkdir(parents=True, exist_ok=True)
dest.mkdir(parents=True, exist_ok=True)
here = Path(__file__).parent

jobs = [l.rstrip("\n").split("\t") for l in open(jobs_path, encoding="utf-8")]
have = {p.stem.lower() for p in dest.glob("*.fuz")}
todo = [j for j in jobs if j[1].lower() not in have and not (wav_dir / f"{j[0]}.wav").exists()]
print(f"{len(jobs)} lines, {len(have)} packed, {len(todo)} to speak on {processes} x {threads} threads", flush=True)

BELOW_NORMAL = 0x00004000
running = []
for n in range(processes):
    part = todo[n::processes]
    if not part:
        continue
    shard = wav_dir / f"_shard{n}.tsv"
    shard.write_text("".join(f"{j[0]}\t{j[2]}\n" for j in part), encoding="utf-8", newline="")
    log = open(wav_dir / f"_shard{n}.log", "w", encoding="utf-8")
    running.append(subprocess.Popen(
        [probe, "--batch", model, str(shard), str(wav_dir), voice, str(threads), "44100", "0.8"],
        stdout=log, stderr=subprocess.STDOUT, creationflags=BELOW_NORMAL))

start = time.time()
while any(p.poll() is None for p in running):
    time.sleep(120)
    done = sum(1 for _ in wav_dir.glob("*.wav"))
    print(f"{time.strftime('%H:%M')} spoken so far: {done} (this run: {time.time() - start:.0f} s)", flush=True)
print("speech finished, exit codes:", [p.returncode for p in running], flush=True)

r = subprocess.run([sys.executable, str(here / "pack_fuz.py"), jobs_path, str(wav_dir), str(dest), encoder, "8"],
                   capture_output=True, text=True, encoding="utf-8")
print(r.stdout.strip() or r.stderr.strip()[-400:], flush=True)
