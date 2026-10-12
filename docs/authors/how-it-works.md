# How It Works

## Speech into a running game

Skyrim only plays loose files it saw at launch, but it re-reads a file's bytes every time it plays one. The mod ships a few silent placeholder wavs; the plugin writes each generated line into the next one and asks [AudioUtil](https://github.com/crajjjj/AudioUtil) to play that file. AudioUtil needs no change for this.

That is why a placeholder added by raising a `slots` setting only starts working after the next restart: the game has to see the file at launch first.

## Three lanes

Each kind of speech has its own placeholder files and its own exclusivity channel. A file is only rewritten once its channel has stopped the line that used it, which is why the lanes must not share one.

| Lane | Placeholder files | Played by | Config |
|---|---|---|---|
| Narration | `Data\Sound\AudioUtilTTS\_tts_slotNN.wav` | AudioUtil, flat | `[playback]` |
| Character speech | `Data\Sound\AudioUtilTTS\_tts_speechNN.wav` | AudioUtil, at the actor, with lipsync | `[speech]` |
| Dialogue | `Data\Sound\Voice\AudioUtilTTS\_tts_dialogNN.wav` and `.lip` | The game's dialogue system | `[dialogue]` |

## The dialogue patch

Dialogue uses the same trick one step earlier. When the game builds a dialogue response it works out the path of the response's voice file. The plugin patches that step, and for a response with no voice file it swaps the path for a placeholder under `Data\Sound\Voice\AudioUtilTTS` (a wav and a lip) that holds the generated line. From there the game does everything it does for a recorded line: timing, subtitles, the voice volume slider and the mouth.

The patch is two redirected calls in one function. If the game code at those two places is not what the plugin expects, it patches nothing and says so in the log; the rest of the plugin works as before. With `[dialogue] enable = false` at launch the game is left unpatched.

### Listed or said right away

The game builds the responses of every topic in the dialogue menu when it fills the menu, and says one when the player picks it, seconds later or never. So the plugin tells two cases apart:

- **Listed**: the dialogue menu is open, or a topic arrives right behind other responses. A cached line is copied into its placeholder off the game thread. A new line gets a placeholder only if it should be generated within about 2.5 seconds; otherwise it is left as the game built it and generated for the cache.
- **Said right away**: a first response is due this instant, so a new one stays unvoiced this once. A later response of the same topic has the time the earlier ones take to say.

The game is never made to wait for a line.

A placeholder goes to the line used longest ago, and never to one used in the last half minute: nothing tells the plugin when the game is done with a file.

### The cache

A generated dialogue line is kept in `Data\SKSE\Plugins\AudioUtilTTS\cache`. Lines are cached per model file, so replacing a model's files starts that voice afresh by itself.

### The mouth animation

The speech model returns audio and no timing, so there are no real phonemes to animate. For a dialogue line the plugin writes a `.lip` file in the game's own format beside the wav, with mouth shapes guessed from the loudness of the line. For character speech AudioUtil's lipsync drives the mouth from the loudness directly.

### Greetings spoken late

A new greeting cannot be handed to the game in time, and the patch above is not told who speaks. The same goes for any line of one response that is said right away, and from 0.2.3 on all of those are treated alike. With `[dialogue] greetings` on, the plugin remembers such a line by its topic info, waits for the game's own report of that topic info being said (which names the speaker), and plays the generated audio as a character line at that actor. Such a line is generated ahead of every other waiting dialogue line. The same reports tell when its speaker has begun another line, which is when a late greeting is dropped; an open dialogue menu alone does not drop it.

### Greetings made ahead

With `[dialogue] prepare_greetings` on, the plugin looks at nearby NPCs, picks the greetings each would say the way the game picks them, and builds those lines early so the patch above sees them. They are generated behind every line the game actually asked for, and only into the cache.

## Threads

Models are loaded and lines are generated on background threads. The game's own thread only hands over the text and starts the finished audio. One worker generates lines, dialogue and the other two lanes in turn; a second thread copies cached dialogue lines into their placeholders.

## The speech runtime

Speech comes from [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) with ONNX Runtime, on the CPU. Both are linked statically, so the mod is a single DLL. That is deliberate: a separate `onnxruntime.dll` would share its module name with the copy other mods load.

The runtime exits the process on some malformed model files. The plugin checks a model folder before handing it over, but cannot catch everything, which is why a new model should be tried with the [probe](tools.md#the-probe) first.

## The load pipeline

On the game's data-loaded message the plugin reads `AudioUtilTTS.toml`, the `model.toml` files and the voice type patches, makes sure the placeholder files exist, finds AudioUtil, starts the worker (which loads the default model in the background), and, when dialogue is enabled, installs the patch and starts the greeting look. On a new game or a load it drops every line still on the way.
