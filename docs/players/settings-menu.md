# In-Game Menu

Added in 0.3.3. With [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) installed, AudioUtilTTS has pages in the framework's Mod Control Panel: open the framework's menu with its hotkey and pick **AudioUtilTTS**. Every setting of `AudioUtilTTS.toml` is there, with a Status page to see what is installed and to try a voice.

The framework is optional. Without it the plugin runs as before, and you edit `AudioUtilTTS.toml` by hand.

## The pages

| Page | What it holds |
|---|---|
| **Status** | Whether speech is ready, the default voice, the voice models loaded and installed, this session's dialogue and the line cache. A voice to try, reload, and clearing the cache |
| **Dialogue** | Unvoiced dialogue, new lines said right away, the mouth (`[dialogue]`) |
| **Voice types** | The `[voicetype]` lines of your file, and a form to add one |
| **Voice** | The master switch, the default voice, threads and models in memory (`[general]`, `[model]`, `[voice]`) |
| **Presets** | Each `[preset.<name>]` of your file: model, voice, speed and volume |
| **Narration & speech** | Lines that wait, the longest line, the speaker's mouth (`[playback]`, `[speech]`) |

Settings that are only for an unusual setup, such as placeholder file counts, AudioUtil groups and channels or a model's language, are in each page's **Advanced** group, which starts closed. Hover over a setting to see what it does, its default and its key in the file.

## How a change is saved

- **Written as you change it.** A slider when you let go of it, a typed value when you leave the field, a checkbox at once. Only the value is replaced: the comments and the layout of the file stay as they were. A text value is written in single quotes (`'MaleNord'`), which TOML reads the same as double quotes.
- **Used from the next line on.** Most settings are read again for every line, so a change is heard at once. Two kinds take longer:
    - **Speech on**, **Model folder**, **Threads per line** and the model settings under **Advanced** load the voice models again. That takes a second or two, and lines on their way are dropped.
    - The placeholder file counts and **Cache size limit** take effect after the next game start. The menu marks them "(after a restart)".
    - Unvoiced dialogue **On** stops it at once when you turn it off. Turned on again, it works at once when it was on at game start, and after the next game start when it was not.
- **reset** puts a setting back to the value the plugin ships with, and **Reset this page to defaults** does it for a whole page after asking. The shipped values come from `AudioUtilTTS.defaults.toml`, which the plugin archive installs beside the config.
- **A setting your file leaves out** shows the value the plugin uses for it. Changing it adds the line.
- **A preset setting left out** shows the default voice's value, which is what the preset uses. Changing it sets it for that preset alone.

## Edits outside the menu

The menu reads the file again each time it is opened, so a change made in a text editor meanwhile shows up, and the plugin applies it too. **Reload from disk** at the foot of a page does the same while the menu is open.

A file that does not parse is never written. The page says why; fix the file in a text editor and press **Reload from disk**.

## Which file it edits

It edits `Data\SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml`, the copy the game sees. When another mod ships its own copy of that file, such as a voice pack with settings of its own, that copy wins in a mod manager, and it is the one the menu changes. Reinstalling or updating that mod puts its own copy back.

Not edited here: the presets a mod brings in `presets\*.toml`, [voice type patches](voices.md#voice-type-patches) and a model's own `model.toml`. They still apply as before; your own `AudioUtilTTS.toml` wins over them.

## The Status page

- **Status.** Five lines: whether speech is ready (and why not), the default voice, how many voice models are loaded and installed, how many dialogue lines were given a voice or left unvoiced this session, and the size of the line cache. **Details** below them opens the full text `autts status` and `autts dialogue` print.
- **Try a voice.** Type a line and pick a voice: **(the default voice)** or one of the presets.
    - **Speak it as narration** speaks the line flat, as a mod's narration would be.
    - **Speak it as the selected NPC** has the NPC you clicked in the console before opening the menu say it, at them, with their mouth moving. With nobody selected, your own character says it. With **(the default voice)** picked, they speak in the voice of their own voice type.
    - **Which voice does the selected NPC get?** shows their voice type and the voice it leads to, as `autts voice` does in the console.
- **Reload settings and voices** does what `autts reload` does: the file is read again and every model is loaded afresh.
- **Clear the dialogue line cache** does what `autts clearcache` does, after asking. Every line is generated again the next time it comes up.

A line you try is generated like any other, so it plays a moment later. Its sender in `AudioUtilTTS.log` is `Menu`.

## The Voice types page

The lines your file has are listed by voice type: change the preset a voice type speaks with, or empty it so that voice type is never spoken for.

**Add a line** gives a voice type a preset: type the voice type's ID, pick the preset and press **Add the line**. To find an NPC's voice type, select them in the console and run `autts voice`, or use **Which voice does the selected NPC get?** on the Status page.

A voice archive named after a voice type needs no line here, and neither do the voice types a patch or a model's `model.toml` covers. A line here wins over all of them; see [Voice Types](voices.md#voice-types) for the order.

## When the menu is not there

`AudioUtilTTS.log` says why at game start:

| The log says | What to do |
|---|---|
| `Menu: off, SKSE Menu Framework is not installed` | Install SKSE Menu Framework, or keep editing the file by hand |
| `Menu: off, this SKSE Menu Framework lacks: ...` | Update SKSE Menu Framework |
| `Menu: registered, section "AudioUtilTTS", ...` | The pages are there: look for **AudioUtilTTS** in the framework's Mod Control Panel |
