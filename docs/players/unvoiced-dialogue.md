# Unvoiced Dialogue

Plenty of mod dialogue ships without voice files. With `[dialogue] enable = true` (the default) the plugin speaks those lines: when the game builds a dialogue line and no voice file exists for it, the line is generated in the voice of the speaker's voice type and handed to the game as that line's voice file.

Added in 0.2.0. Nothing has to be called or patched by the dialogue mod.

## Which lines are spoken

Out of the box the plugin speaks for the voice types you have set a voice up for; see [Voice Types](voices.md#voice-types). Every other voice type stays as it was. `[dialogue] fallback = true` extends it to all of them with a generic voice, picked by sex.

!!! info "Changed in 0.2.1"
    `fallback` is off by default. In 0.2.0 it was on, so every unvoiced line was spoken with the preset `female` or `male`.

- **Only lines with no recorded voice.** The plugin asks the game whether a voice file exists (loose or in an archive, `.fuz`, `.wav` or `.xwm`). If one does, nothing happens.
- **Placeholder lines are left alone.** A line with nothing to say, such as `...`, `(...)`, `[...]` or a lone dash, stays silent: it holds no letter and no digit once markup and directions are gone. `autts dialogue` counts them.
- **Text is cleaned up first.** Markup in angle brackets is dropped, stage directions such as `(sighs)` too (`[dialogue] skip_directions`), and text in the Windows-1252 encoding that English game data uses is converted, so curly apostrophes and accented letters are read correctly.
- **Very long lines are cut.** Speech stops at the last sentence that fits into `[playback] max_chars` (400 characters by default), while the subtitle shows the whole line. Raise the setting if a mod writes longer responses.

## How a spoken line behaves

- **The game plays the line itself**, like a recorded one. It lasts as long as the speech, the subtitle follows the usual setting, the voice volume slider applies, the sound comes from the speaker, and the conversation moves on when the line ends.
- **The mouth moves.** A mouth animation is written beside the audio in the game's own `.lip` format. Its shapes are guessed from the loudness of the line, so it reads as talking without forming the actual sounds. `[dialogue] lip_strength` sets how far the mouth opens.
- **Each line is generated once.** Generated lines are kept in `Data\SKSE\Plugins\AudioUtilTTS\cache` (1024 MB by default) and come from there at once every later time.

## When a line is ready

A new line has to be generated first, and the game is never made to wait for that. What happens depends on how the line comes up:

| The line is... | First time | Every later time |
|---|---|---|
| **Listed in the dialogue menu** | Voiced if it can be generated within about 2.5 seconds, the time it takes to pick a topic. Otherwise unvoiced this once | Voiced, from the cache |
| **A greeting** | Spoken a moment late, as soon as it is generated; see [Greetings spoken late](#greetings-spoken-late) | Voiced, from the cache |
| **Said right away** otherwise (a scene, a remark in passing) | Unvoiced this once, and generated for next time | Voiced, from the cache |
| **A later line of the same topic** | Generated while the earlier lines play | Voiced, from the cache |

- **Lines in the dialogue menu are prepared while you read it.** The game builds the lines of every topic the menu shows when it fills the menu. The plugin puts the unvoiced ones in place in the background, the first line of each topic first.
- **Preparing a menu costs CPU.** Every unvoiced line the menu lists is generated once, whether it is picked or not. With a fast model that is a few seconds of work per menu; with the default model it is about two seconds per line on `[model] threads` cores.

## Greetings spoken late

Added in 0.2.1. A greeting is the plainest case of a line said right away. With `[dialogue] greetings = true` (the default) a new one is generated the moment the NPC says it and spoken as soon as it is ready, about as late as a line takes to generate. From the second time on it comes from the cache and the game says it on time itself.

- **It plays as character speech**, not through the game's dialogue: at the NPC, with their mouth moving, on the `[speech]` group and channel. The subtitle has been shown already, when the game said the line.
- **Not only greetings.** From 0.2.3 on this goes for every line of one response that is said right away: a greeting, the line that opens a conversation, a remark in passing, whatever kind of topic it is filed under. A line with further responses behind it is not spoken late.
- **First in line.** A line waiting to be spoken late is generated before every other dialogue line that waits, and played before any other line.
- **Also once you are talking.** A line that opened a conversation is still spoken after the dialogue menu has come up. When its speaker goes on to another line while it is being spoken, it is cut short, as the game cuts its own lines; that needs AudioUtil 0.9.29 or newer, with an older one the two lines overlap.
- **A long line starts with its first sentence.** With `[dialogue] greetings_split = true` (the default, added in 0.2.3) a line of several sentences is generated in two parts: the first sentence is spoken as soon as it is ready, the rest is generated meanwhile and follows it. That is for this first time only; the cache gets the whole line.
- **Not at any price.** It is not spoken when it would be more than `[dialogue] greetings_max_late` seconds late (3 by default, 0.5 to 10; added in 0.2.3), when the speaker is gone, when the speaker has gone on to another line, or when another character line is being spoken. The line is in the cache for the next time either way.

These settings can be changed with `autts reload`, and `autts dialogue` counts the lines spoken late.

## Greetings made ahead

Added in 0.2.1. `[dialogue] prepare_greetings = true` (off by default) gets a new greeting spoken on time instead of late.

While you walk around, the plugin looks at the NPCs within about 20 m, one every two seconds, works out which greetings each would say right now, and generates the unvoiced ones in the background, behind every line the game actually asked for. When the NPC then greets you, the line is in the cache and is spoken at once.

- **It is a guess.** The greetings are picked the way the game picks them (the greeting topics of running quests by priority, the first topic info whose conditions hold), but conditions can change before the NPC speaks.
- **It costs CPU for NPCs that never greet you.** Up to three lines per NPC, each NPC looked at once every five minutes, each line made once.
- **Only where it can matter.** NPCs whose voice type has no voice are skipped, and nothing happens while the game is paused, in a menu or in combat.

The setting can be switched with `autts reload`, no restart needed.

## Beside other dialogue mods

Works beside **Fuz Ro D'oh** and **Dynamic Dialogue Replacer**. Fuz Ro D'oh keeps covering the lines this plugin leaves alone.

One gap: a line the plugin expected to have ready, picked before it is, plays as silence that once, and Fuz Ro D'oh does not step in for it.

## Checking on it

```
autts dialogue
```

shows whether it is running, how many lines this session were given a voice, left unvoiced, had nothing to say or were generated, how many greetings were spoken late or made ahead, and the size of the cache. `AudioUtilTTS.log` says what was done about a line that stays unvoiced and why; see [Troubleshooting & Logs](troubleshooting.md#a-dialogue-line-stays-unvoiced).

| Command | Does |
|---|---|
| `autts dialogue` | Show the status |
| `autts dialogue 0` | Switch unvoiced-dialogue speech off for this session (`1` = on) |
| `autts clearcache` | Delete the cached lines, so each is generated afresh |

!!! note "Switching it off for good"
    `[dialogue] enable = false` leaves the game unpatched. A change of that key needs a game restart; `autts dialogue 0` only switches it off for the session.

## The cache

- Lives in `Data\SKSE\Plugins\AudioUtilTTS\cache`, limited by `[dialogue] cache_max_mb` (1024 by default, `0` = no limit). The lines used longest ago are dropped first.
- A line is cached per model file. After replacing a model's files, run `autts reload`: the lines of the old model are not played again, and nothing has to be cleared by hand.
- The folder can be deleted freely. Every line is simply generated again the next time it comes up.
