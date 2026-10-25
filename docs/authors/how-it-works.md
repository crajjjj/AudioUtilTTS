# How It Works

## Speech into a running game

Skyrim only plays loose files it saw at launch, but it re-reads a file's bytes every time it plays one. The mod ships a few silent placeholder wavs; the plugin writes each generated line into the next one and asks [AudioUtil](https://github.com/crajjjj/AudioUtil) to play that file. AudioUtil needs no change for this.

That is why a placeholder added by raising a `slots` setting only starts working after the next restart: the game has to see the file at launch first.

## Four lanes

Each kind of speech has its own placeholder files and its own exclusivity channel. A file is only rewritten once its channel has stopped the line that used it, which is why the lanes must not share one.

| Lane | Placeholder files | Played by | Config |
|---|---|---|---|
| Narration | `Data\Sound\AudioUtilTTS\_tts_slotNN.wav` | AudioUtil, flat | `[playback]` |
| Character speech | `Data\Sound\AudioUtilTTS\_tts_speechNN.wav` | AudioUtil, at the actor, with lipsync | `[speech]` |
| The player's lines | `Data\Sound\AudioUtilTTS\_tts_playerNN.wav` | AudioUtil, at the player, with lipsync | `[player]` |
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

A new greeting cannot be handed to the game in time, and the patch above is not told who speaks. The same goes for any line of one response that is said right away. With `[dialogue] greetings` on, the plugin remembers such a line by its topic info, waits for the game's own report of that topic info being said (which names the speaker), and plays the generated audio as a character line at that actor. Such a line is generated ahead of every other waiting dialogue line. The same reports tell when its speaker has begun another line, which is when a late greeting is dropped; an open dialogue menu alone does not drop it.

### Greetings made ahead

With `[dialogue] prepare_greetings` on, the plugin looks at nearby NPCs, picks the greetings each would say the way the game picks them, and builds those lines early so the patch above sees them. They are generated behind every line the game actually asked for, and only into the cache.

## The player's lines

Dragonborn ReVoiced holds a conversation open while the player's line is said, and offers the lines its voice pack has no recording for to a *voice provider*: another SKSE plugin, found through DBReV.dll's exports (`include/API/DBReV_API.h`, a declaration-only header that links against nothing). The plugin registers as that provider on the game's data-loaded message, when `[player] enable` is on.

One line, from this side: DBReV calls the plugin on the main thread with the topic text; the plugin takes the line (and queues it) or declines it (DBReV then handles it as it always has), and returns. The line goes first on the worker, behind only the line being generated that moment; a line said before comes from the cache and starts from inside the call. When the audio starts, the plugin tells DBReV the line's length, and DBReV holds the dialogue for that long plus the user's post-line delay. A line that cannot be spoken after all is given back at once, so the conversation never waits for nothing. When DBReV lets the conversation go on, by a skip, another topic or the dialogue closing, the plugin stops the line where it plays and drops it where it waits.

DBReV also announces every player line it plays, its own included, and the plugin listens: an NPC line being spoken late by the conversation partner stops when the player begins a line, as the game stops its own lines.

## SkyrimNet's lines

SkyrimNet sends each NPC line to a voice engine over HTTP. For its XTTS engine that is a server at `http://localhost:8020`, and the plugin is that server: `src/XttsHttp.cpp` answers the requests an XTTS server answers (`/tts_to_audio/`, `/create_and_store_latents`, `/health`) on 127.0.0.1 only, and `src/XttsServer.cpp` opens it while SkyrimNet.dll is loaded and `[skyrimnet] enable` is on. The socket is opened for exclusive use, so a port another XTTS server holds is refused and left to it.

A request names the speaker by voice type. The line goes onto the worker ahead of every other line but the player's own, is generated in that voice type's voice, and goes back to SkyrimNet as a wav, which SkyrimNet plays itself; nothing is played or cached here. A voice sample SkyrimNet sends is answered 404 while voice cloning is off, which is how an XTTS server says it takes none: SkyrimNet then names its speakers by voice type. With voice cloning on, the sample is kept, and that speaker's lines are spoken like it on the Pocket model.

## Threads

Models are loaded and lines are generated on background threads. The game's own thread only hands over the text and starts the finished audio. One worker generates lines, dialogue and the other two lanes in turn; a second thread copies cached dialogue lines into their placeholders.

## The speech runtime

Speech comes from [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) with ONNX Runtime, on the CPU. Both are linked statically, so the mod is a single DLL. That is deliberate: a separate `onnxruntime.dll` would share its module name with the copy other mods load.

The runtime exits the process on some malformed model files. The plugin checks a model folder before handing it over, but cannot catch everything, which is why a new model should be tried with the [probe](tools.md#the-probe) first.

## The load pipeline

On the game's data-loaded message the plugin reads `AudioUtilTTS.toml`, the preset files, the `model.toml` files and the voice type patches, makes sure the placeholder files exist, finds AudioUtil, starts the worker (which loads the default model in the background, when one is installed), and, when dialogue is enabled, installs the patch and starts the greeting look. On a new game or a load it drops every line still on the way.
