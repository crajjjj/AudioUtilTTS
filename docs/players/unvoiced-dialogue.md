# Unvoiced Dialogue

Plenty of mod dialogue ships without voice files. When the game is about to say such a line, the plugin generates it in the voice of the speaker's voice type and hands it to the game as the line's voice file. The dialogue mod needs no change.

It does this for the voice types that have a voice:

- **a voice archive named after the voice type**, with nothing to configure;
- **a [voice type patch](voices.md#voice-type-patches)**, for a character with a voice type of their own;
- **`[dialogue] fallback = true`**, a generic voice by sex for every voice type that has none (needs the default voice);
- **your own choice** in `AudioUtilTTS.toml`; see [Voice Types](voices.md#voice-types).

Any other voice type stays unvoiced. `autts voice` on an NPC shows which case they are.

## Which lines are spoken

- **Only lines with no voice file**, loose or in an archive. A line that has one is never touched, even a silent one another mod ships.
- **Not a line with nothing to say**, such as `...` or `(sighs)`.
- **Text is cleaned first.** Markup and stage directions are left out (`[dialogue] skip_directions`), accented letters and curly quotes are read correctly, and your [text replacements](configuration.md#text-replacements) apply.
- **Very long lines are cut** at the last sentence that fits into `[playback] max_chars` (400 characters); the subtitle still shows all of it.

## How a spoken line behaves

It plays like a recorded line: from the speaker, as long as the speech lasts, with the game's subtitles and voice volume slider, and the conversation moves on when it ends. The mouth moves with the loudness of the line (`[dialogue] lip_strength` sets how far).

Each line is generated once and kept in a cache; every later time it comes from there at once.

## When a line is ready

A new line has to be generated first, about two seconds a sentence with the default model, and the game never waits for it:

| The line is... | First time | Later times |
|---|---|---|
| **Listed in the dialogue menu** | Voiced if it can be ready within about 2.5 seconds, the time it takes to pick a topic; otherwise unvoiced this once. A line of one response the NPC begins before it is ready is spoken a moment late, as a greeting is | From the cache |
| **Said right away** (a greeting, the line that opens a conversation, a remark in passing) | Spoken a moment late, as soon as it is ready; see [Greetings spoken late](#greetings-spoken-late) | From the cache |
| **A later line of the same topic** | Generated while the earlier lines play | From the cache |

Lines in the dialogue menu are prepared while you read it, the first line of each topic first. That costs CPU: every unvoiced line the menu lists is generated once, picked or not.

## Greetings spoken late

A line said right away cannot be ready in time. With `[dialogue] greetings = true` (the default) it is generated at once and spoken as soon as it is ready, a moment after its subtitle. From the second time on it comes from the cache, on time.

- **It plays as character speech**: at the NPC, mouth moving, on the `[speech]` group and channel.
- **It goes first**: it is generated before every other waiting dialogue line.
- **A long line starts with its first sentence** (`greetings_split`), and the rest follows. Only that first time.
- **It stops** when its speaker goes on to another line, or when you begin a line of your own with Dragonborn ReVoiced, as the game's own lines do (needs AudioUtil 0.9.29).
- **It is not spoken** when it would be later than `greetings_max_late` (4 seconds, counted from when the NPC begins the line; with a voiced player, Dragonborn ReVoiced or DBVO, that is after your own line), when the speaker is gone or has moved on, or while another character line is spoken. It is in the cache for next time either way.
- **Not for a line with further responses behind it.** That one stays unvoiced the first time, and the responses after it are generated while it plays.

## Greetings made ahead

`[dialogue] prepare_greetings = true` (off by default) gets a new greeting spoken on time. The plugin looks at the NPCs near you, works out which greetings each would say, and generates the unvoiced ones in the background.

- **It is a guess**: what an NPC says can change before they say it.
- **It is heavy, most of all in towns**: up to three lines for every NPC you come near whose voice type has a voice, whether they greet you or not, and their voices are loaded for it.
- **It pauses** while the game is paused, in a menu or in combat.

## Beside other dialogue mods

Works beside **Fuz Ro D'oh** and **Dynamic Dialogue Replacer**; Fuz Ro D'oh keeps covering the lines this plugin leaves alone. One gap: a menu line the plugin expected to have ready, picked before it is, plays as silence that once.

## Checking on it

| Command | Does |
|---|---|
| `autts dialogue` | Whether it runs; how many lines were given a voice, left unvoiced, spoken late or generated this session; the cache size |
| `autts dialogue 0` | Switch it off for this session (`1` = on) |
| `autts clearcache` | Delete the cached lines, so each is generated afresh |

`AudioUtilTTS.log` says why a line stayed unvoiced; see [Troubleshooting & Logs](troubleshooting.md#a-dialogue-line-stays-unvoiced). `[dialogue] enable = false` leaves the game unpatched from the next start.

## The cache

- `Data\SKSE\Plugins\AudioUtilTTS\cache`, limited by `[dialogue] cache_max_mb` (1024 MB, `0` = no limit). The lines used longest ago go first.
- Lines are cached per model file: after replacing a model's files, `autts reload` is all it takes.
- The folder can be deleted freely; lines are simply generated again.
- Under Mod Organizer 2 it is created where your profile puts files the game writes: Overwrite, or the mod you set for that.
