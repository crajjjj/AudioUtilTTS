# SkyrimNet

[SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin) gives NPCs AI dialogue, and sends every line an NPC says to a voice engine. One of its engines is XTTS, a speech server it reaches at `http://localhost:8020`. AudioUtilTTS answers at that address itself, so SkyrimNet's NPCs speak in the voices installed here, with no server to install or keep running.

## Setting it up

1. Install AudioUtilTTS and SkyrimNet.
2. Turn it on: **Speak SkyrimNet's lines** on the menu's **SkyrimNet** page, or `[skyrimnet] enable = true` in `AudioUtilTTS.toml` and then `autts reload`. It is off unless switched on.
3. In SkyrimNet's settings, choose **XTTS** as the voice engine and leave its address as it is, `http://localhost:8020`.

That is all. `autts status` shows `SkyrimNet answered at http://localhost:8020` once the game has loaded.

Run no other XTTS server on that port at the same time. One that is already running when the game starts keeps the port, and SkyrimNet goes on using it: AudioUtilTTS finds that something answers there, logs it and stays out of the way. One started after the game finds the port taken while AudioUtilTTS answers. To use both, give AudioUtilTTS another port (`[skyrimnet] port`) and point SkyrimNet's XTTS address at the one you want.

## Which voice

SkyrimNet names each speaker by their voice type, `malenord` for a male Nord. That voice type finds its voice the way an [unvoiced dialogue](unvoiced-dialogue.md) line does (the seven steps in [Voices, Presets & Voice Types](voices.md)), so an NPC sounds the same in both. A voice type with no voice of its own gets the preset `female` or `male` by the sex in its name (`FemaleNord`, `MaleBrute`), else the default voice: unlike unvoiced dialogue, SkyrimNet always gets a voice, whatever `[dialogue] fallback` says. A voice type your `[voicetype]` lines set to never be spoken for gets a moment of silence.

The line is cleaned as dialogue is: stage directions such as `*smiles*` are left out (`[dialogue] skip_directions`) and your [text replacements](configuration.md#text-replacements) apply. It is never cut short: SkyrimNet shows the whole line as a subtitle, so `[playback] max_chars` does not apply.

## What happens

- **Each line is generated when SkyrimNet asks for it**, on your CPU: about a second for a short line with the default model, more for a long one (see [What to Expect](what-to-expect.md)).
- **SkyrimNet's lines go first.** SkyrimNet waits for each line, so it is generated ahead of narration and unvoiced dialogue waiting their turn. Only your own lines with Dragonborn ReVoiced come before it, and greetings that are generated already and only wait to be played.
- **A line SkyrimNet stops waiting for is dropped**, so a withdrawn request never holds up the next one.
- **SkyrimNet plays the line**, with its own subtitles, mouth movement and volume. AudioUtilTTS only makes the audio, so `[speech]` and the AudioUtil groups do not apply. The volume of a preset or a `model.toml` is part of the audio.
- **Nothing is cached.** An AI line is seldom said twice.
- **A line that is not ready within 60 seconds is given up**, and loading a save gives up the lines on their way.

## Voice cloning

SkyrimNet can send a short voice sample for a speaker, and an XTTS server then speaks that speaker like the sample. This is off unless you switch it on.

- **Off**: the samples are declined, and SkyrimNet names its speakers by voice type instead. The voices installed here speak.
- **On** (`[skyrimnet] voice_cloning = true`, or **Voice cloning** on the menu's SkyrimNet page): the samples are kept in `Data\SKSE\Plugins\AudioUtilTTS\skyrimnet\`, one wav per speaker (the 10 seconds Pocket uses), and each of those speakers is spoken like its sample with the Pocket model (`[skyrimnet] clone_model`, see [Pocket voices](models.md#pocket-voices)). Without the Pocket model installed, the samples are declined as if cloning were off. A sample that cannot be spoken like for any reason leaves that speaker with their voice type's voice, and the log says why.

Kyutai's terms for Pocket ask for the consent of the person whose voice is copied. Samples kept stay in the folder when cloning is switched off again, unused; delete the folder to remove them.

## Settings

All under `[skyrimnet]` in `AudioUtilTTS.toml`, and on the menu's **SkyrimNet** page; see [Configuration Reference](configuration.md#skyrimnet).

| Key | Default | Meaning |
|---|---|---|
| `enable` | `false` | Answer SkyrimNet, only while it is installed |
| `port` | `8020` | The port of SkyrimNet's XTTS address, 1024 to 65535 |
| `voice_cloning` | `false` | Keep SkyrimNet's voice samples and speak like them |
| `clone_model` | `"pocket-tts"` | The Pocket model folder voice samples are spoken with |

## Checking on it

- **`autts status`**, or the menu's Status page, has a SkyrimNet part: `answered at http://localhost:8020` with the lines answered this session, or why not: `not installed`, `off ([skyrimnet] enable)`, `port 8020 is taken by another program`.
- **`AudioUtilTTS.log`** has a line for each line: the speaker's name, the voice it got and why, its length and how long it took. A line that could not be made says why.

If SkyrimNet's NPCs stay silent: check that SkyrimNet's voice engine is XTTS and its address `http://localhost:8020`, that `autts status` says `answered`, and SkyrimNet's own log for the requests it sent.
