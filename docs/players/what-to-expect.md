# What to Expect

Speech is generated when it is asked for, on your CPU. That shapes how the mod behaves in play.

## How it behaves

- **Background threads, no effect on frame rate seen.** Models are loaded and lines are generated on background threads. The game's own thread only hands over the text and starts the finished audio, so rendering is not held up while a line is made. In game no drop in frame rate was seen (i7-12700KF, default model, two threads). `[model] threads` sets how many cores a line may use.
- **Speech starts late.** With the default model a sentence takes about two seconds on two threads of an i7-12700KF, and longer while the game is busy.
- **One line at a time, nothing piles up.** A line that arrives while another is being generated or spoken is dropped, so a mod that sends lines faster than they can be spoken is not read out back to back. Each dropped line is noted in `AudioUtilTTS.log` with the reason. Narration and character speech are counted separately. To let lines wait instead, raise `[playback] max_pending`.
- **Narration and character speech are separate.** Narration plays flat and moves no mouth. A character line plays at the actor and moves their mouth. Neither goes through the game's dialogue system, so there are no dialogue subtitles for them.
- **Unvoiced dialogue is played by the game.** It behaves like a recorded line: subtitles, the voice volume slider and the conversation flow are the game's. See [Unvoiced Dialogue](unvoiced-dialogue.md).

!!! info "Changed in 0.2.1"
    - Lines that find their lane busy are dropped (`max_pending = 0`). In 0.2.0 one line could wait by default (`max_pending = 1`).
    - The game is never made to wait for a dialogue line. 0.2.0 could hold the game for up to a quarter of a second for a line said right away (`[dialogue] wait_ms`); that setting is gone.

## Character speech

A mod, or `autts sayactor "text"` in the console, makes an actor say a line.

- **The mouth follows the loudness of the line** (AudioUtil's lipsync). It opens and closes with the speech; it does not form real phonemes. This needs AudioUtil 0.9.26 or newer, otherwise the mouth stays still.
- **AudioUtil's guards still apply.** A gag, a worn tongue item, a dialogue with the player or another mod's claim on the mouth all keep it still.
- **A character and the narrator can talk at once.** Character lines use their own channel and placeholder files (`[speech]` in the config). Two characters cannot: a line for one that arrives while another is speaking is dropped, or waits its turn with `[playback] max_pending` raised.
- **The voice is as close to the NPC as the model you give it.** A generated voice matches an NPC's recorded voice only as far as the model set up for that voice type does. See [Voices, Presets & Voice Types](voices.md#voice-types).
- **A speaker who left is not spoken for.** If the speaker is gone or unloaded by the time the line is ready, it is dropped.

## Cost

Measured outside the game on an i7-12700KF, for lines of about five seconds of speech. Nothing here uses the GPU, and all of it runs on background threads. In a running game on that machine no drop in frame rate was noticed while lines were generated (observed, not benchmarked).

| | Kokoro (default) | Piper voice |
|---|---|---|
| Memory, settled after a few lines | about 625 MB | about 350 MB |
| Time to generate a line, 2 threads | about 2 s | about 0.25 s |
| CPU while generating | 2 cores fully busy | 2 cores fully busy |
| CPU between lines | none | none |
| Time to load, on a background thread | 0.8 s | 0.6 s |

- **A line costs CPU only while it is generated**, plus a short wind-down right after (about a third of a CPU-second with two threads).
- **`[model] threads` trades burst width for length.** One thread keeps a single core busy for about 3.2 s per line, two for about 1.9 s, four for about 1.2 s.
- **Memory does not grow with use.** It was flat from the fifth line to the thirtieth.
- **Presets on the default model cost nothing extra.** A preset or a voice type on another model adds that model's memory once it has been used. See [Voice Models](models.md#how-many-fit-in-memory).
