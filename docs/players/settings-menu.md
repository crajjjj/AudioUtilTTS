# In-Game Menu

With [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) installed, open the framework's menu and pick **AudioUtilTTS**: every setting of `AudioUtilTTS.toml`, a status page, and a voice to try. Without the framework, edit the file by hand as before.

## The pages

| Page | What it holds |
|---|---|
| **Status** | Whether speech is ready, the default voice, the voice models loaded and installed, this session's dialogue, your lines, SkyrimNet's lines, the line cache. A voice to try, reload, clearing the cache |
| **Dialogue** | Unvoiced dialogue, new lines said right away, the mouth |
| **Voice types** | The `[voicetype]` lines of your file, and a form to add one |
| **Voice** | The master switch, the default voice, threads and models in memory |
| **Presets** | Each preset of your file: model, voice, speed, volume |
| **Narration & speech** | Lines that wait, the longest line, the speaker's mouth |
| **SkyrimNet** | Speaking SkyrimNet's lines, voice cloning, the port ([SkyrimNet](skyrimnet.md)) |

Rare settings (placeholder counts, AudioUtil groups and channels, a model's language) sit in each page's closed **Advanced** group. Hover over a setting for what it does, its default and its key.

## Saving

- **A change is written at once**: a slider when you let go, a typed value when you leave the field. Only the value changes; the file's comments and layout stay. Text is written in single quotes (`'MaleNord'`), which TOML reads the same.
- **It is used from the next line on.** **Speech on**, **Model folder**, **Threads per line** and the model settings under Advanced reload the voices first, a second or two. Settings marked "(after a restart)" wait for the next game start.
- **reset** puts a setting back to the shipped value; **Reset this page to defaults** does a whole page.
- **A setting your file leaves out** shows the value the plugin uses; changing it adds the line. A preset setting left out shows the default voice's value.
- **Edits made in a text editor** are picked up each time the menu opens, or with **Reload from disk**. A file that does not parse is never written.

The menu edits the copy of `AudioUtilTTS.toml` the game sees. When a voice pack ships its own copy, that one is edited, and reinstalling the pack puts its values back. Preset files, voice type patches and `model.toml` files are not edited here.

## The Status page

- **Try a voice**: type a line, pick **(the default voice)** or a preset, then **Speak it as narration** or **Speak it as the selected NPC** (the NPC clicked in the console before opening the menu, else you). With **(the default voice)**, an NPC speaks in their own voice type's voice.
- **Which voice does the selected NPC get?** answers what `autts voice` does.
- **Reload settings and voices** is `autts reload`; **Clear the dialogue line cache** is `autts clearcache`, after asking.
- **Details** opens the full text of `autts status` and `autts dialogue`.

## The Voice types page

Change the preset a voice type speaks with, or empty it so the voice type is never spoken for. **Add a line** gives another voice type a preset: type its ID (what `autts voice` shows), pick the preset, press **Add the line**. A voice archive named after a voice type needs no line here.

## When the menu is not there

| `AudioUtilTTS.log` says | What to do |
|---|---|
| `Menu: off, SKSE Menu Framework is not installed` | Install it, or keep editing the file by hand |
| `Menu: off, this SKSE Menu Framework lacks: ...` | Update SKSE Menu Framework |
| `Menu: registered, section "AudioUtilTTS", ...` | Look for **AudioUtilTTS** in the framework's Mod Control Panel |
