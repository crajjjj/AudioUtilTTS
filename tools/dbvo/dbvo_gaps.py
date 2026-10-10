#!/usr/bin/env python3
"""Which player dialogue lines of an MO2 load order have no DBVO voice file?

Reads every active plugin's dialogue topics (the text the player clicks: a topic's
name, or a response's own prompt when it has one), turns each line into the file
name a legacy DBVO pack uses (Sound\\DBVO\\<pack>\\<line>.fuz), and checks it
against the files the enabled mods provide.

Usage: dbvo_gaps.py <MO2 instance> <profile> <game Data> <pack id> <out dir>
Writes <out>/missing.tsv (file name, text, plugin, form id), <out>/summary.txt
"""
import struct
import sys
import zlib
from collections import Counter
from pathlib import Path

INSTANCE, PROFILE, DATA, PACK, OUT = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3]), sys.argv[4].lower(), Path(sys.argv[5])
OUT.mkdir(parents=True, exist_ok=True)
VANILLA = ["skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm", "dragonborn.esm"]
# topic categories the player never clicks: 3 combat, 5 detection, 7 miscellaneous (hellos, idles)
SKIP_CATEGORIES = {3, 5, 7}


# ------------------------------------------------------------------ the mod list
def enabled_mods():
    """Enabled mod folders, highest priority first (the order of modlist.txt)."""
    mods = []
    for line in (INSTANCE / "profiles" / PROFILE / "modlist.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("+"):
            mods.append(INSTANCE / "mods" / line[1:])
    return mods


MODS = enabled_mods()
ROOTS = [INSTANCE / "overwrite"] + MODS + [DATA]   # first hit wins

plugin_path, strings_path, archives = {}, {}, {}
pack_files = set()
pack_mods = Counter()
for root in ROOTS:
    try:
        entries = list(root.iterdir())
    except OSError:
        continue
    for entry in entries:
        low = entry.name.lower()
        if low.endswith((".esp", ".esm", ".esl")):
            plugin_path.setdefault(low, entry)
        elif low.endswith(".bsa"):
            archives.setdefault(low, entry)
        elif low == "strings" and entry.is_dir():
            for s in entry.iterdir():
                strings_path.setdefault(s.name.lower(), s)
        elif low == "sound" and entry.is_dir():
            pack = entry / "DBVO" / PACK
            if pack.is_dir():
                before = len(pack_files)
                for f in pack.iterdir():
                    if f.suffix.lower() == ".fuz":
                        pack_files.add(f.stem.lower())
                pack_mods[root.name] = len(pack_files) - before

active = list(VANILLA)
for line in (INSTANCE / "profiles" / PROFILE / "plugins.txt").read_text(encoding="utf-8", errors="replace").splitlines():
    if line.startswith("*") and line[1:].lower() not in active:
        active.append(line[1:].lower())


# -------------------------------------------------------------------------- BSA
class Bsa:
    def __init__(self, path):
        import lz4.frame
        self.lz4 = lz4.frame
        self.f = open(path, "rb")
        magic, version, _off, self.flags, folders, files, _fnl, filenl, _ff = struct.unpack("<4sIIIIIIII", self.f.read(36))
        assert magic == b"BSA\x00" and version == 105
        records = [struct.unpack("<QIIQ", self.f.read(24)) for _ in range(folders)]
        entries = []
        for _hash, count, _pad, _offset in records:
            n = self.f.read(1)[0]
            folder = self.f.read(n)[:-1].decode("cp1252").lower()
            for _ in range(count):
                _fh, size, offset = struct.unpack("<QII", self.f.read(16))
                entries.append((folder, size, offset))
        names = self.f.read(filenl).split(b"\x00")[:files]
        self.files = {folder + "\\" + name.decode("cp1252").lower(): (size, offset)
                      for (folder, size, offset), name in zip(entries, names)}

    def read(self, path):
        size, offset = self.files[path]
        compressed = bool(self.flags & 0x4) != bool(size & 0x40000000)
        size &= 0x3FFFFFFF
        self.f.seek(offset)
        data = self.f.read(size)
        if self.flags & 0x100:
            data = data[1 + data[0]:]
        return self.lz4.decompress(data[4:]) if compressed else data


def read_strings(blob, prefixed):
    count, _size = struct.unpack_from("<II", blob, 0)
    base = 8 + count * 8
    table = {}
    for i in range(count):
        sid, off = struct.unpack_from("<II", blob, 8 + i * 8)
        start = base + off + (4 if prefixed else 0)
        end = blob.find(b"\x00", start)
        table[sid] = blob[start:end if end >= 0 else len(blob)].decode("cp1252", errors="replace")
    return table


_bsa_cache = {}


def strings_for(plugin):
    """The plugin's English string table (all three files merged), or None."""
    stem = plugin.rsplit(".", 1)[0]
    table, found = {}, False
    for ext, prefixed in ((".strings", False), (".dlstrings", True), (".ilstrings", True)):
        name = f"{stem}_english{ext}"
        blob = None
        if name in strings_path:
            blob = strings_path[name].read_bytes()
        else:
            for bsa_name in (f"{stem}.bsa", "skyrim - interface.bsa", "skyrim - patch.bsa"):
                if bsa_name not in archives:
                    continue
                try:
                    bsa = _bsa_cache.setdefault(bsa_name, Bsa(archives[bsa_name]))
                    if f"strings\\{name}" in bsa.files:
                        blob = bsa.read(f"strings\\{name}")
                        break
                except Exception:
                    continue
        if blob:
            found = True
            for k, v in read_strings(blob, prefixed).items():
                table.setdefault(k, v)
    return table if found else None


# ---------------------------------------------------------------------- plugins
def subrecords(data):
    p, big = 0, None
    while p + 6 <= len(data):
        tag, size = struct.unpack_from("<4sH", data, p)
        p += 6
        if tag == b"XXXX":
            big = struct.unpack_from("<I", data, p)[0]
            p += size
            continue
        if big is not None:
            size, big = big, None
        yield tag, data[p: p + size]
        p += size


def dial_group(path):
    """(localized, masters, raw bytes of the DIAL top group or b'')."""
    with open(path, "rb") as f:
        head = f.read(24)
        if len(head) < 24 or head[:4] != b"TES4":
            return False, [], b""
        size, flags = struct.unpack_from("<II", head, 4)
        masters = [body.split(b"\x00")[0].decode("cp1252").lower() for tag, body in subrecords(f.read(size)) if tag == b"MAST"]
        while True:
            head = f.read(24)
            if len(head) < 24:
                return bool(flags & 0x80), masters, b""
            tag, gsize, label = struct.unpack("<4sI4s", head[:12])
            if tag != b"GRUP":
                return bool(flags & 0x80), masters, b""
            if label == b"DIAL":
                return bool(flags & 0x80), masters, f.read(gsize - 24)
            f.seek(gsize - 24, 1)


def record_at(buf, pos):
    tag, size = struct.unpack_from("<4sI", buf, pos)
    flags, form_id = struct.unpack_from("<II", buf, pos + 8)
    data = buf[pos + 24: pos + 24 + size]
    if flags & 0x00040000:
        try:
            data = zlib.decompress(data[4:])
        except zlib.error:
            data = b""
    return tag, flags, form_id, data, pos + 24 + size


topics = {}    # key -> [text, category, plugin]
prompts = {}   # info key -> [text, topic key, plugin]
problems = Counter()

for plugin in active:
    path = plugin_path.get(plugin)
    if path is None:
        problems["plugin file not found"] += 1
        continue
    try:
        localized, masters, group = dial_group(path)
    except OSError:
        problems["plugin unreadable"] += 1
        continue
    if not group:
        continue
    table = strings_for(plugin) if localized else None
    if localized and table is None:
        problems["localized plugin without an English string table"] += 1
        continue
    order = masters + [plugin]

    def key(form_id, order=order):
        index = form_id >> 24
        return (order[index] if index < len(order) else order[-1], form_id & 0xFFFFFF)

    def text_of(body, table=table, localized=localized):
        if localized:
            return table.get(struct.unpack("<I", body)[0], "") if len(body) == 4 else ""
        return body.split(b"\x00")[0].decode("cp1252", errors="replace")

    def walk(buf, parent):
        pos = 0
        while pos + 24 <= len(buf):
            if buf[pos: pos + 4] == b"GRUP":
                size, label, kind = struct.unpack_from("<I4sI", buf, pos + 4)
                child_parent = key(struct.unpack("<I", label)[0]) if kind == 7 else parent
                walk(buf[pos + 24: pos + size], child_parent)
                pos += size
                continue
            tag, flags, form_id, data, pos = record_at(buf, pos)
            k = key(form_id)
            if tag == b"DIAL":
                full, category = "", None
                for sub, body in subrecords(data):
                    if sub == b"FULL":
                        full = text_of(body)
                    elif sub == b"DATA" and len(body) >= 2:
                        category = body[1]
                if flags & 0x20:
                    topics.pop(k, None)
                else:
                    old = topics.get(k)
                    topics[k] = [full, category if category is not None else (old[1] if old else 0), plugin]
            elif tag == b"INFO":
                prompt = ""
                for sub, body in subrecords(data):
                    if sub == b"RNAM":
                        prompt = text_of(body)
                if flags & 0x20:
                    prompts.pop(k, None)
                else:
                    prompts[k] = [prompt, parent, plugin]

    walk(group, None)


# --------------------------------------------------------------------- the lines
def file_name(text):
    """A legacy DBVO pack names a line's file after its text: spaces and the
    characters Windows forbids in a file name become underscores."""
    return "".join("_" if c in ' ?:"<>|/\\*\t\r\n' else c for c in text)


lines = {}  # lower-case file name -> (text, plugin, form id)
for (origin, local), (full, category, plugin) in topics.items():
    if full.strip() and category not in SKIP_CATEGORIES:
        lines.setdefault(file_name(full).lower(), (full, plugin, f"{local:06X}:{origin}"))
for (origin, local), (prompt, parent, plugin) in prompts.items():
    if not prompt.strip():
        continue
    topic = topics.get(parent)
    if topic is not None and topic[1] in SKIP_CATEGORIES:
        continue
    lines.setdefault(file_name(prompt).lower(), (prompt, plugin, f"{local:06X}:{origin}"))

covered = {n: v for n, v in lines.items() if n in pack_files}
missing = {n: v for n, v in lines.items() if n not in pack_files}
templated = {n: v for n, v in missing.items() if "<" in v[0]}
plain = {n: v for n, v in missing.items() if "<" not in v[0]}

with open(OUT / "missing.tsv", "w", encoding="utf-8", newline="") as f:
    f.write("file\ttext\tplugin\tform\n")
    for name, (text, plugin, form) in sorted(plain.items(), key=lambda kv: (kv[1][1], kv[0])):
        flat = " ".join(text.split())   # a few lines hold tabs or line breaks
        f.write(f"{file_name(text)}\t{flat}\t{plugin}\t{form}\n")

with open(OUT / "all_lines.tsv", "w", encoding="utf-8", newline="") as f:
    for name, (text, plugin, form) in lines.items():
        f.write(f"{text.replace(chr(9), ' ').replace(chr(10), ' ').replace(chr(13), ' ')}\t{plugin}\t{form}\n")
(OUT / "pack_names.txt").write_text("\n".join(sorted(pack_files)) + "\n", encoding="utf-8")

vanilla_lines = {n: v for n, v in lines.items() if v[1] in VANILLA}
vanilla_hit = sum(1 for n in vanilla_lines if n in pack_files)
by_plugin_missing = Counter(v[1] for v in plain.values())
by_plugin_all = Counter(v[1] for v in lines.values())
chars = sum(len(v[0]) for v in plain.values())

report = []
report.append(f"active plugins: {len(active)}, with dialogue: {len({v[1] for v in lines.values()})}")
report.append(f"problems: {dict(problems) or 'none'}")
report.append(f"voice files for pack '{PACK}': {len(pack_files)} in {len(pack_mods)} mods")
report.append(f"player lines in the load order: {len(lines)}")
report.append(f"  have a voice file: {len(covered)} ({100 * len(covered) / max(len(lines), 1):.1f}%)")
report.append(f"  no voice file: {len(missing)}")
report.append(f"    plain text (can be generated ahead): {len(plain)}, {chars} characters")
report.append(f"    with a <tag> filled in at run time (cannot): {len(templated)}")
report.append(f"check of the file-name rule on the base game: {vanilla_hit} of {len(vanilla_lines)} lines found "
              f"({100 * vanilla_hit / max(len(vanilla_lines), 1):.1f}%)")
report.append("plugins with the most unvoiced lines (missing / all):")
for plugin, n in by_plugin_missing.most_common(40):
    report.append(f"  {n:5d} / {by_plugin_all[plugin]:5d}  {plugin}")
report.append("examples of unvoiced lines:")
for name, (text, plugin, form) in list(sorted(plain.items(), key=lambda kv: kv[1][1]))[:: max(1, len(plain) // 25)][:25]:
    report.append(f"  [{plugin}] {text}")
report.append("base-game lines without a file (first 15):")
for name, (text, plugin, form) in [kv for kv in vanilla_lines.items() if kv[0] not in pack_files][:15]:
    report.append(f"  [{plugin}] {text!r}")
(OUT / "summary.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
print("\n".join(report))
