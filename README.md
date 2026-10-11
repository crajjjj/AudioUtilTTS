# AudioUtilTTS

Live text-to-speech for Skyrim SE. A mod hands it a line of text and the line is
spoken in game a moment later, through [AudioUtil](https://github.com/crajjjj/AudioUtil).
It also speaks game dialogue that has no recorded voice, in a voice you choose
per voice type. The voice model runs inside the plugin, on the CPU, with no
internet connection and no helper program. Speech is generated on background
threads, not on the game's own, and no effect on frame rate was seen in game.

## Requirements

- Skyrim SE / AE with SKSE64 and Address Library
- AudioUtil 0.9.9 or newer (it plays narration and character speech); 0.9.26 or
  newer for the mouth to move on character speech. Dialogue is played by the
  game itself and does not need it.
- ConsoleUtil Extended, only for the `autts` test commands

## Installing

Take both archives from the [latest release](https://github.com/crajjjj/AudioUtilTTS/releases/latest)
and install them like any mod:

- `AudioUtilTTS-<version>.zip`: the plugin, its config, scripts and console
  commands.
- `AudioUtilTTS-Voice-kokoro-en-v0_19.zip`: the default voice model. It is a
  separate download because it is large and does not change with the plugin.

Without a voice model the plugin loads but stays silent; `autts status` says
why.

## Using it from a mod

The simplest way needs no dependency on this mod at all. Send a mod event from
any Form:

```papyrus
SendModEvent("AudioUtilTTS_Speak", "The road to Whiterun lay open.")
```

It is spoken with the configured default voice. Without AudioUtilTTS installed
the event goes nowhere. Use `SendModEvent` (the Form function), not
`ModEvent.Create`.

To choose who speaks, name a preset after a colon:

```papyrus
SendModEvent("AudioUtilTTS_Speak:whisper", "Not so loud.")
```

A preset is a voice the user defines in the config under one name (see
[Presets](#presets)). An unknown name falls back to the default voice, so a mod
can ask for a role without knowing what the user has installed.

Those are narration: flat, with no speaker. To make a character say a line, let
that actor send `AudioUtilTTS_Say`. The line is positioned at them and their
mouth moves:

```papyrus
akSpeaker.SendModEvent("AudioUtilTTS_Say", "Not so loud.")
akSpeaker.SendModEvent("AudioUtilTTS_Say:whisper", "Not so loud.")
```

With no preset named, the speaker gets the voice set up for their voice type,
else the preset `female` or `male` by their sex when the config defines those.
See [Character speech](#character-speech) and [Voice types](#voice-types).

Game dialogue needs no call at all, see [Unvoiced dialogue](#unvoiced-dialogue).

To learn whether a line was accepted, or to pass a voice number, volume or speed
directly, call the natives and guard them:

```papyrus
If SKSE.GetPluginVersion("AudioUtilTTS") >= 0
    AudioUtilTTS.SpeakAs("Not so loud.", "whisper")
    AudioUtilTTS.Speak("The road to Whiterun lay open.", 9)
    AudioUtilTTS.SpeakActor(akSpeaker, "Not so loud.")
EndIf
```

`Speak` and `SpeakAs` are narration and take no actor. `SpeakActor` is the
character form: the actor comes first, then the text, then an optional preset.

When a line starts playing, the mod event `AudioUtilTTS_Spoken` fires with
`strArg` = the text, `numArg` = the AudioUtil instance handle and the sender =
the speaking actor (none for narration). The full API is documented in
`papyrus\Source\AudioUtilTTS.psc`.

## What to expect

- **Background threads, no effect on frame rate.** Models are loaded and lines
  are generated on background threads. The game's own thread only hands over
  the text and starts the finished audio, so rendering is not held up while a
  line is made. In game no drop in frame rate was seen (i7-12700KF, default
  model, two threads; `[model] threads` sets how many cores a line may use).
  The one time the game itself waits is a dialogue line said right away, for
  up to a quarter of a second, once per line (see
  [Unvoiced dialogue](#unvoiced-dialogue)).
- **Speech starts late.** A line is generated when it is asked for. With the
  default model a sentence takes about two seconds on two threads of an
  i7-12700KF, and longer while the game is busy.
- **One line at a time, nothing piles up.** A line that arrives while another
  is being generated or spoken is dropped, so a mod that sends lines faster
  than they can be spoken is not read out back to back. Each dropped line is
  noted in `AudioUtilTTS.log` with the reason. Narration and character speech
  are counted separately. To let lines wait instead, raise
  `[playback] max_pending`.
- **Narration and character speech are separate.** Narration plays flat and moves
  no mouth. A character line plays at the actor and moves their mouth. Neither
  goes through the game's dialogue system, so there are no dialogue subtitles.

## Character speech

`AudioUtilTTS.SpeakActor(akSpeaker, "text")`, or the actor sending
`AudioUtilTTS_Say`, makes that actor say the line.

- The mouth follows the loudness of the line (AudioUtil's lipsync). It opens and
  closes with the speech; it does not form real phonemes. Needs AudioUtil
  0.9.26 or newer, otherwise the mouth stays still.
- AudioUtil's guards still apply: a gag, a worn tongue item, a dialogue with the
  player or another mod's claim on the mouth all keep it still.
- Character lines use their own channel and placeholder files (`[speech]` in the
  config), so a character and the narrator can talk at once. Two characters
  cannot: a line for one that arrives while another is speaking is dropped
  (or waits its turn, with `[playback] max_pending` raised).
- A generated voice matches an NPC's recorded voice only as far as the model you
  give that voice type does (see [Voice types](#voice-types)).
- If the speaker is gone or unloaded by the time the line is ready, it is
  dropped.

## Unvoiced dialogue

New in 0.2.0. Plenty of mod dialogue ships without voice files. With
`[dialogue] enable = true` (the default) the plugin speaks those lines: when
the game builds a dialogue line and no voice file exists for it, the line is
generated in the voice of the speaker's voice type and handed to the game as
that line's voice file. Out of the box that covers the voice types you have set
a voice up for (see [Voice types](#voice-types)); `[dialogue] fallback = true`
extends it to every other voice type with a generic voice.

- **Only lines with no recorded voice.** The plugin asks the game whether a
  voice file exists (loose or in an archive, `.fuz`, `.wav` or `.xwm`). If one
  does, nothing happens.
- **The game plays the line itself**, like a recorded one. It lasts as long as
  the speech, the subtitle follows the usual setting, the voice volume slider
  applies, the sound comes from the speaker, and the conversation moves on when
  the line ends.
- **The mouth moves.** A mouth animation is written beside the audio in the
  game's own `.lip` format. Its shapes are guessed from the loudness of the
  line, so it reads as talking without forming the actual sounds.
- **Each line is generated once.** Generated lines are kept in
  `Data\SKSE\Plugins\AudioUtilTTS\cache` (256 MB by default) and come from
  there at once every later time.
- **Lines in the dialogue menu are prepared while you read it.** The game builds
  the lines of every topic the menu shows when it fills the menu. The plugin
  puts the unvoiced ones in place in the background, the first line of each
  topic first, and the game never waits for them. A line generated before is
  copied from the cache. A new one is voiced this time only if it should be
  generated within about 2.5 seconds, the time it takes to pick a topic;
  otherwise it is left unvoiced this once and voiced from the next time on.
- **A line said right away is voiced from its second time.** A scene line, a
  greeting or a remark in passing cannot be prepared, and the game is never
  made to wait for one: a new line stays unvoiced that once and is generated
  for the next time. Later lines of the same topic are generated while the
  earlier ones play. Greetings can be made ahead instead (see below).
- **Preparing a menu costs CPU.** Every unvoiced line the menu lists is
  generated once, whether it is picked or not. With a fast model that is a few
  seconds of work per menu; with the default model it is about two seconds per
  line on `[model] threads` cores.
- **Works beside Fuz Ro D'oh and Dynamic Dialogue Replacer.** Fuz Ro D'oh keeps
  covering the lines this plugin leaves alone. One gap: a line the plugin
  expected to have ready, picked before it is, plays as silence that once,
  and Fuz Ro D'oh does not step in for it.
- **Text is cleaned up first.** Markup in angle brackets is dropped, stage
  directions such as `(sighs)` too (`[dialogue] skip_directions`), and text in
  the Windows-1252 encoding that English game data uses is converted, so curly
  apostrophes and accented letters are read correctly.
- **Placeholder lines are left alone.** A line with nothing to say, such as
  `...`, `(...)`, `[...]` or a lone dash, stays silent: it holds no letter and
  no digit once markup and directions are gone. `autts dialogue` counts them.

### Greetings made ahead

A greeting is the plainest case of a line said right away: with a slow model it
is silent the first time and spoken from the second. `[dialogue]
prepare_greetings = true` (off by default) closes that gap. While you walk
around, the plugin looks at the NPCs within about 20 m, one every two seconds,
works out which greetings each would say right now, and generates the unvoiced
ones in the background, behind every line the game actually asked for. When the
NPC then greets you, the line is in the cache and is spoken at once.

- **It is a guess.** The greetings are picked the way the game picks them (the
  greeting topics of running quests by priority, the first topic info whose
  conditions hold), but conditions can change before the NPC speaks.
- **It costs CPU for NPCs that never greet you.** Up to three lines per NPC,
  each NPC looked at once every five minutes, each line made once.
- **Only where it can matter.** NPCs whose voice type has no voice are skipped,
  and nothing happens while the game is paused, in a menu or in combat.

`autts dialogue` shows whether it is running, how many lines were given a
voice, left unvoiced and generated this session (and how many of those were
greetings made ahead), and the size of the cache.
`AudioUtilTTS.log` says what was done about a line that stays unvoiced and why.

### Voice types

Every NPC has a voice type, and that is what an unvoiced line is spoken for.
A voice type finds its voice in this order:

1. its line in `[voicetype]`, naming a preset
2. a preset named exactly like the voice type
3. a model folder named exactly like the voice type (its first voice)
4. the preset `female` or `male` by sex, only with `[dialogue] fallback = true`

```toml
[voicetype]
MaleNord = "malenord"
FemaleEvenToned = "eventoned"
FemaleYoungEager = "eventoned"   # several voice types can share one voice
CrDogVoice = ""                  # never spoken for

[preset.malenord]
model = "nord-male"        # a model folder with one voice

[preset.eventoned]
model = "my-voices"          # one model holding many voices
voice = 4
```

The quickest setup is no config at all: a model folder called
`models\MaleNord` speaks for the voice type `MaleNord`. The name is the voice
type's editor ID, as xEdit shows it; case is ignored. Select an NPC in the
console and run `autts voice` to see their voice type and the voice it leads to,
and `autts sayactor "Some text"` to hear it.

By default (`fallback = false`) only the voice types you set up are spoken, and
the rest stay as they were. With `fallback = true` every other voice type is
spoken with the preset `female` or `male` by its sex.

A model per voice type costs memory per loaded model (see [Cost](#cost));
`[model] max_loaded` caps how many are kept. One model holding many voices
costs the memory once.

After replacing a model's files, run `autts reload`. Lines are cached per model
file, so the lines of the old model are not played again.

The same lookup gives a character their voice for `SpeakActor` and
`AudioUtilTTS_Say` when no preset is named.

## Cost

Measured outside the game on an i7-12700KF, for lines of about five seconds of
speech. Nothing here uses the GPU, and all of it runs on background threads. In
a running game on that machine no drop in frame rate was noticed while lines
were generated (observed, not benchmarked).

| | Kokoro (default) | Piper voice |
|---|---|---|
| Memory, settled after a few lines | about 625 MB | about 350 MB |
| Time to generate a line, 2 threads | about 2 s | about 0.25 s |
| CPU while generating | 2 cores fully busy | 2 cores fully busy |
| CPU between lines | none | none |
| Time to load, on a background thread | 0.8 s | 0.6 s |

- A line costs CPU only while it is generated, plus a short wind-down right
  after (about a third of a CPU-second with two threads). `[model] threads` trades burst
  width for length: one thread keeps a single core busy for about 3.2 s per
  line, four threads keep four busy for about 1.2 s.
- Memory does not grow with use: it was flat from the fifth line to the
  thirtieth.
- Presets on the default model cost nothing extra. A preset on another model
  adds that model's memory once it has been used.

## Console commands

| Command | Does |
|---|---|
| `autts say "text"` | Speak a line with the configured voice |
| `autts sayv "text" 9` | Speak a line with voice number 9 of the default model |
| `autts sayas "text" whisper` | Speak a line through the preset `whisper` |
| `autts sayactor "text"` | Make the console-selected actor (or the player) say a line |
| `autts sayactoras "text" whisper` | The same through the preset `whisper` |
| `autts sayevent "text"` | The same through the `AudioUtilTTS_Say` mod event |
| `autts voice` | Show the voice type of the console-selected actor (or the player) and the voice it leads to |
| `autts dialogue` | Show the status of unvoiced-dialogue speech |
| `autts dialogue 0` | Switch unvoiced-dialogue speech off for this session (`1` = on) |
| `autts clearcache` | Delete the cached dialogue lines |
| `autts event "text"` | Speak a line through the mod event |
| `autts eventas "text" whisper` | Speak a line through the mod event with a preset |
| `autts status` | Show whether speech is ready, and why not |
| `autts stop` | Drop waiting lines and cut the current one |
| `autts reload` | Re-read the config and reload the model |
| `autts enable 0` | Switch speech off for this session (`1` = on) |

## Configuration

`Data\SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml` holds the default model and
voice, the presets, the voice types, the dialogue settings, thread count and
playback behaviour. Every key is commented in the file. Volume also follows
AudioUtil's `narration` group.

### Voices

A voice is a number inside a model. The default model, `kokoro-en-v0_19`, has 11
English voices:

| Number | Name | Voice |
|---|---|---|
| 0 | af | American female (the model's default blend) |
| 1 | af_bella | American female |
| 2 | af_nicole | American female, whispered |
| 3 | af_sarah | American female |
| 4 | af_sky | American female |
| 5 | am_adam | American male |
| 6 | am_michael | American male |
| 7 | bf_emma | British female (the plugin's default) |
| 8 | bf_isabella | British female |
| 9 | bm_george | British male |
| 10 | bm_lewis | British male |

Set the default with `voice = N` under `[voice]`, or give a preset its own
`voice = N`. To hear one in game, run `autts sayv "Some text" 4` with any number
from the table.

Other models number their voices differently; most Piper voices have just one,
number 0. Larger multi-language Kokoro builds are on the same model list but
have not been tried with this plugin.

### Presets

A preset gives a model, a voice number, a speed and a volume one name that mods
ask for:

```toml
[preset.whisper]
voice = 2

[preset.fast]
model = "vits-piper-en_US-lessac-medium"
speed = 1.1
```

Anything a preset leaves out comes from the defaults. The shipped config defines
`narrator`, `female`, `male` and `whisper`, all on the default model.

A preset may name a different model. That model is loaded the first time a line
asks for it, which delays that one line by about a second, and then stays in
memory (see [Cost](#cost)). `[model] max_loaded` (default 3, up to 32) caps how
many are kept; the least recently used one is unloaded first, never the default.

### Models

Models go into `Data\SKSE\Plugins\AudioUtilTTS\models\<folder>`. Kokoro
(`kokoro-en-v0_19`, the default) and VITS models such as the Piper voices from
the [sherpa-onnx model list](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models)
are supported. `kokoro-en-v0_19` and `vits-piper-en_US-lessac-medium` have been
checked outside the game, loaded together.

A model that the runtime cannot read can close the game while loading, because
the runtime exits on some malformed files. Check a new model outside the game
first with the probe (see below).

## How it works

Skyrim only plays loose files it saw at launch, but it re-reads a file's bytes
every time it plays one. The mod ships a few silent placeholder wavs
(`Data\Sound\AudioUtilTTS\_tts_slotNN.wav`); the plugin writes each generated
line into the next one and asks AudioUtil to play that file. AudioUtil needs no
change for this.

Dialogue uses the same trick one step earlier. When the game builds a dialogue
response it works out the path of the response's voice file; the plugin patches
that step, and for a response with no voice file it swaps the path for a
placeholder under `Data\Sound\Voice\AudioUtilTTS` (a wav and a lip) that holds
the generated line. From there the game does everything it does for a recorded
line. The patch is two redirected calls in one function; if the game code at
those two places is not what the plugin expects, it patches nothing and says so
in the log.

The speech runtime (sherpa-onnx with ONNX Runtime) is linked statically, so the
mod is a single DLL. That is deliberate: a separate `onnxruntime.dll` would
share its module name with the copy other mods load.

## Building

```
git submodule update --init --recursive
powershell -ExecutionPolicy Bypass -File scripts\fetch-deps.ps1
xmake f -m release
xmake                      # DLL -> dist\SKSE\Plugins\AudioUtilTTS.dll
xmake build papyrus        # scripts -> dist\Scripts
xmake build package        # release archives -> Release\
```

- Needs VS 2022 (v143, C++23) and xmake 3.0 or newer.
- `fetch-deps.ps1` downloads the prebuilt runtime libraries (about 1 GB
  unpacked, into `extern\`) and the default voice model (about 350 MB, into
  `dist\`). Neither is tracked in git.
- `AudioUtilTTS.ppj` holds machine-local paths and is not tracked either.
- Both build modes link the release static CRT, because the prebuilt runtime
  libraries do.

`xmake build probe` builds a small console program that runs the same speech
core outside the game:

```
xmake run probe dist\SKSE\Plugins\AudioUtilTTS\models\kokoro-en-v0_19 "Some text" 7 out.wav
```

It prints how long the line took to generate and writes `out.lip` beside the
wav: the mouth animation a dialogue
line with that audio would get. AudioUtil's LipSim plays the pair.

## Licenses

The plugin archive carries the full notices and license texts in
`SKSE\Plugins\AudioUtilTTS\licenses`.

AudioUtilTTS is GPLv3 (see `LICENSE`). It links
[sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) (Apache 2.0),
[ONNX Runtime](https://github.com/microsoft/onnxruntime) (MIT) and, through
sherpa-onnx, [espeak-ng](https://github.com/espeak-ng/espeak-ng) (GPLv3). Voice
models carry their own license, in the model folder.
