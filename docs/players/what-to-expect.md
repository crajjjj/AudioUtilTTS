# What to Expect

Speech is generated on your CPU when it is asked for. That shapes how the mod behaves.

## How it behaves

- **Speech starts late.** With the default model a sentence takes about two seconds on two threads of an i7-12700KF, longer while the game is busy.
- **The frame rate is not held up.** Lines are generated on background threads; no drop in frame rate was seen in game. `[model] threads` sets how many cores a line may use.
- **One line at a time.** A line that arrives while another is being generated or spoken is dropped, so a mod that sends lines faster than they can be spoken is not read out back to back. `AudioUtilTTS.log` notes each dropped line. Narration and character speech count separately. `[playback] max_pending` lets lines wait instead.
- **Narration plays flat**, with no speaker and no subtitles.
- **Unvoiced dialogue is played by the game** like a recorded line, with its subtitles and voice volume. See [Unvoiced Dialogue](unvoiced-dialogue.md).

## Character speech

A mod, or `autts sayactor "text"` in the console, has an actor say a line.

- **It plays at the actor and moves their mouth** with the loudness of the line (AudioUtil lipsync, not real phonemes).
- **AudioUtil's guards apply**: a gag, a worn tongue item, a dialogue with the player or another mod's claim on the mouth keep it still.
- **A character and the narrator can talk at once.** Two characters cannot: the second line is dropped, or waits with `max_pending` raised.
- **The voice sounds like the NPC** only as far as the model set up for their voice type does; see [Voice Types](voices.md#voice-types).
- **A speaker who left is not spoken for.**

## Cost

Measured on an i7-12700KF for lines of about five seconds of speech. Nothing uses the GPU.

| | Kokoro (default) | Piper voice | Pocket |
|---|---|---|---|
| Memory, settled after a few lines | about 625 MB | about 350 MB | about 600 MB |
| Time to generate a line, 2 threads | about 2 s | about 0.25 s | about 1.4 s |
| CPU while generating | 2 cores fully busy | 2 cores fully busy | 2 cores fully busy |
| CPU between lines | none | none | none |
| Time to load, in the background | 0.8 s | 0.6 s | 0.6 s |

- **More threads, shorter bursts**: one thread takes about 3.2 s for a line, two about 1.9 s, four about 1.2 s.
- **Memory does not grow with use.**
- **Presets on the default model cost nothing extra.** Another model adds its memory once a line has used it; see [How many fit in memory](models.md#how-many-fit-in-memory).
