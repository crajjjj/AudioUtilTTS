#!/usr/bin/env python3
"""Turn the load order's player lines into a work list for the lines no voice
pack covers.

File name of a line (legacy DBVO pack), as the 55,000 existing files show it:
the text up to the first " (" (so "(Persuade)", "(Sex)", "(400 gold)" at the end
do not count), trimmed, with spaces and the characters Windows forbids in a file
name turned into underscores.

What is spoken: the whole line without stage directions, i.e. without anything
in (parentheses), [brackets] or *asterisks*.

Usage: prep_lines.py <all_lines.tsv> <pack_names.txt> <out dir>
Writes <out>/jobs.tsv   id, file name, spoken text, plugin   (the work list)
       <out>/speak.tsv  id, spoken text                       (input of probe --batch)
"""
import re
import sys
from collections import Counter
from pathlib import Path

lines = [l.rstrip("\n").split("\t") for l in open(sys.argv[1], encoding="utf-8")]
lines = [l for l in lines if len(l) == 3]
have = {n for n in open(sys.argv[2], encoding="utf-8").read().split("\n") if n}
out = Path(sys.argv[3])
MAX_NAME = 180


def file_name(text):
    head = text.split(" (")[0].strip()
    return "".join("_" if c in ' ?:"<>|/\\*\t\r\n' else c for c in head)


def spoken(text):
    t = re.sub(r"\([^()]*\)|\[[^\[\]]*\]|\*[^*]*\*", " ", text)
    t = t.replace("…", "...").strip(" \"'“”")
    return " ".join(t.split())


skipped = Counter()
jobs, seen = [], set()
for text, plugin, _form in lines:
    name = file_name(text)
    key = name.lower()
    if key in have:
        skipped["has a voice file"] += 1
    elif "<" in text:
        skipped["text filled in at run time (<Alias=...>)"] += 1
    elif not name.strip("_. "):
        skipped["no file name left"] += 1
    elif key in seen:
        skipped["same file as another line"] += 1
    else:
        say = spoken(text)
        if sum(c.isalpha() for c in say) < 2:
            skipped["nothing to say (only a direction)"] += 1
        elif re.fullmatch(r"[A-Za-z0-9_]+", text) and (re.search(r"[a-z][A-Z]|\d|_", text)):
            skipped["an internal name, not a line"] += 1
        elif len(name) > MAX_NAME:
            skipped["file name too long"] += 1
        elif any(ord(c) > 0x24F for c in say):
            skipped["not Latin script"] += 1
        else:
            seen.add(key)
            jobs.append((name, say, plugin))

jobs.sort(key=lambda j: (j[2], j[0]))
with open(out / "jobs.tsv", "w", encoding="utf-8", newline="") as f, \
        open(out / "speak.tsv", "w", encoding="utf-8", newline="") as g:
    for i, (name, say, plugin) in enumerate(jobs):
        f.write(f"{i:06d}\t{name}\t{say}\t{plugin}\n")
        g.write(f"{i:06d}\t{say}\n")

chars = sum(len(j[1]) for j in jobs)
print(f"player lines: {len(lines)}")
for k, v in skipped.most_common():
    print(f"  {v:6d}  {k}")
print(f"  {len(jobs):6d}  TO GENERATE ({chars} characters, {len({j[2] for j in jobs})} plugins)")
changed = [(n, s) for n, s, _p in jobs if "(" in n or "[" in n][:6]
for n, s in changed:
    print(f"     {n}  ->  \"{s}\"")
