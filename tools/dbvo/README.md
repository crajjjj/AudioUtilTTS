# Filling the gaps of a Dragonborn Voice Over pack

Dragonborn Voice Over plays a recorded file for each player line its voice pack
has, and stays silent for the rest. These scripts find the lines of a load order
that no installed pack covers and generate a voice file for each, in the pack's
own layout and format, so that mod plays them like any recorded line.

Made for legacy packs (`Sound\DBVO\<pack>\<line>.fuz`) under Mod Organizer 2.

## What you need

- `probe.exe` (`xmake build probe`) and a model folder, as for the plugin
- Python 3 with `lz4` and `numpy`
- `xwmaencode.exe` from the Creation Kit (`Tools\Audio` in the game folder)

## Steps

```
python dbvo_gaps.py  <MO2 instance> <profile> <game Data> <pack id> out
python prep_lines.py out\all_lines.tsv out\pack_names.txt out
python generate.py   out\jobs.tsv <probe.exe> <model dir> <voice> wav "<new mod folder>\Sound\DBVO\<pack id>" <xwmaencode.exe> 3 5
```

1. `dbvo_gaps.py` reads the dialogue topics of every active plugin and lists
   the player's lines (`all_lines.tsv`) and the files the enabled packs hold
   (`pack_names.txt`). `summary.txt` says how much is covered, per plugin.
2. `prep_lines.py` writes the work list: a line's file name, and what is spoken
   for it. A direction such as `(Persuade)` or `[Attack]` is not spoken, and a
   line that is nothing else is skipped. Lines with text the game fills in at
   run time (`<Alias=...>`) cannot be made ahead.
3. `generate.py` speaks the list on several processes (here 3, with 5 threads
   each), then packs each line with `pack_fuz.py`: xWMA at 44.1 kHz mono
   48 kbit/s plus the mouth animation, as recorded packs are. It can be stopped
   and started again.

Put the result in a mod folder of its own and enable it. It only holds file
names no other pack has, so its place in the mod order does not matter.

## Notes

- **Voice.** Pick the voice nearest the pack's. Speaker 1 (`af_bella`) of the
  default model is close to the ElevenLabs "Bella" packs.
- **Loudness.** The generated voice is quieter than a recorded pack. Write the
  difference in dB into `wav\_gain_db.txt` before the packing step (5.7 for the
  Bella packs) and each line is raised by that much.
- **File names.** A pack names a line's file after its text: everything up to
  the first `" ("`, trimmed, with spaces and `? : " < > | / \ *` as
  underscores. That rule was read off the files of existing packs.
- **After changing the mod list**, run the three steps again: only the new
  lines are generated.
