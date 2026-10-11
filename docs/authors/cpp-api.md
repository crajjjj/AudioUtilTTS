# C++ API

For SKSE plugins. The DLL exports a small set of C functions that mirror the [Papyrus API](papyrus-api.md), so a plugin can have a line spoken straight from C++, with no script and no mod event in between.

Added in 0.2.2. The reference header is [`include/API/AudioUtilTTSAPI.h`](https://github.com/crajjjj/AudioUtilTTS/blob/main/include/API/AudioUtilTTSAPI.h).

## No link dependency

You do not link against AudioUtilTTS. Copy the header into your project and resolve the exports at runtime. The header is a reference: its declarations carry no `dllimport`, so including it adds no build-time or load-time dependency.

```cpp
#include <Windows.h>
#include "AudioUtilTTSAPI.h"

struct AudioUtilTTSC {
    decltype(&AudioUtilTTS_GetInterfaceVersion) GetInterfaceVersion = nullptr;
    decltype(&AudioUtilTTS_SpeakFrom)           SpeakFrom           = nullptr;
    decltype(&AudioUtilTTS_SpeakActorFrom)      SpeakActorFrom      = nullptr;
    decltype(&AudioUtilTTS_IsNarrationFree)     IsNarrationFree     = nullptr;
};

inline AudioUtilTTSC LoadAudioUtilTTS() {
    AudioUtilTTSC tts;
    HMODULE h = GetModuleHandleA("AudioUtilTTS.dll");   // null = not installed
    if (!h) return tts;
    tts.GetInterfaceVersion = reinterpret_cast<decltype(tts.GetInterfaceVersion)>(GetProcAddress(h, "AudioUtilTTS_GetInterfaceVersion"));
    tts.SpeakFrom           = reinterpret_cast<decltype(tts.SpeakFrom)>(GetProcAddress(h, "AudioUtilTTS_SpeakFrom"));
    tts.SpeakActorFrom      = reinterpret_cast<decltype(tts.SpeakActorFrom)>(GetProcAddress(h, "AudioUtilTTS_SpeakActorFrom"));
    tts.IsNarrationFree     = reinterpret_cast<decltype(tts.IsNarrationFree)>(GetProcAddress(h, "AudioUtilTTS_IsNarrationFree"));
    return tts;
}
```

Call through the pointers, never the `AudioUtilTTS_*` names themselves. Those are declarations of functions that live in another DLL, so a direct call is an unresolved external when your plugin links.

```cpp
AudioUtilTTSC tts = LoadAudioUtilTTS();

if (tts.SpeakFrom) {
    // narration, default voice
    tts.SpeakFrom("MyPlugin", "The road to Whiterun lay open.", "");
    // narration, through the preset "whisper"
    tts.SpeakFrom("MyPlugin", "Not so loud.", "whisper");
}
if (tts.SpeakActorFrom) {
    // the actor says it, in their own voice, mouth moving
    tts.SpeakActorFrom("MyPlugin", speaker, "Not so loud.", "", true);
}
```

!!! tip "When to resolve, when to call"
    `GetModuleHandleA` only sees AudioUtilTTS once its DLL is loaded: resolve on or after SKSE's `kPostLoad` message, or lazily on first use. Do not speak before `kDataLoaded`; nothing is ready until then and every call answers `false`.

## Version gating

| Export | Returns |
|---|---|
| `AudioUtilTTS_GetVersion()` | The mod version, packed `major*10000000 + minor*100000 + patch` (`200002` for 0.2.2) |
| `AudioUtilTTS_GetInterfaceVersion()` | The version of this C++ interface, packed `MMmmpp` (`10000` for 1.0.0). It changes only when exports are added |

Exports are only ever added, never reordered or removed. Resolving one export tells you AudioUtilTTS is installed, not that every export is: an older build leaves a newer name null. Null-check the pointer you are about to call.

## Threading and lifecycle

- **Same threads as the Papyrus natives.** Call from the game thread, an SKSE task or a Papyrus VM thread. The functions are not meant for arbitrary background threads.
- **Null-safe.** A null string means an empty one, and a null speaker makes `SpeakActorFrom` answer `false`.
- **Strings are copied.** Nothing you pass is kept after the call returns.

## How a line behaves

Exactly as through the Papyrus natives, because it is the same code:

- **`sender`** is your plugin's own short name. It is written into `AudioUtilTTS.log` with the line, so a user can see which mod a spoken or dropped line came from. Nothing else depends on it.
- **`text`** is UTF-8, or the Windows-1252 text the game's own strings use. It is cut at `[playback] max_chars`.
- **`preset`** names a voice the user defines in the config; see [Presets](../players/voices.md#presets). Empty means the default voice for narration and the speaker's own voice for a character line. An unknown name falls back to the default voice.
- **The answer is "queued", not "spoken".** `true` means the line was taken. It is generated in the background and starts a moment later.
- **A line can be dropped.** By default a line that arrives while another of its kind is being generated or spoken is not taken, and the call answers `false`. See [What to design around](overview.md#what-to-design-around).

To learn when a line actually starts, listen for the mod event [`AudioUtilTTS_Spoken`](modevents.md#audioutiltts_spoken) on SKSE's mod callback event source.

## Function reference

| Export | Does |
|---|---|
| `bool AudioUtilTTS_IsReady()` | True when a line can be spoken right now: the default model is loaded, AudioUtil is present, a playable slot exists and speech is switched on |
| `bool AudioUtilTTS_IsNarrationFree()` | Would a narration line sent right now be taken? |
| `bool AudioUtilTTS_IsSpeechFree()` | The same for a character line. True is no promise: another mod can take the place a moment later, so still check what the speak call returns |
| `bool AudioUtilTTS_HasPreset(const char* preset)` | True when the config defines `[preset.<preset>]` |
| `bool AudioUtilTTS_SpeakFrom(const char* sender, const char* text, const char* preset)` | Queues a narration line: flat, with no speaker |
| `bool AudioUtilTTS_SpeakActorFrom(const char* sender, RE::Actor* speaker, const char* text, const char* preset, bool lipSync)` | Queues a line the actor says: positioned at them, with their mouth moving when `lipSync` is true and `[speech] lipsync` is on. The line is dropped if the speaker is gone or not loaded by the time it is ready |
| `void AudioUtilTTS_CancelPending()` | Drops every narration and character line that is waiting or being generated. The line already playing carries on |
