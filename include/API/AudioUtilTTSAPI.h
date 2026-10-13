#pragma once

#include <cstdint>

namespace RE
{
	class Actor;
}

// ======================================================================================
// AudioUtilTTS - native C++ inter-plugin API
// ======================================================================================
//
// A C++ mirror of the AudioUtilTTS Papyrus functions, for SKSE plugins that want a line
// spoken straight from C++: same behaviour as the script API, no Papyrus round trip and
// no mod event.
//
// Consumers do NOT link against AudioUtilTTS - resolve the exports at runtime:
//
//     auto h = GetModuleHandleA("AudioUtilTTS.dll");              // null if not installed
//     if (h) {
//         // decltype(&Fn) takes each pointer type straight from this header, so a
//         // signature change here becomes a compile error in your plugin instead of a
//         // silent mismatch. It inspects the declaration only - no link dependency.
//         auto speak = reinterpret_cast<decltype(&AudioUtilTTS_SpeakFrom)>(
//                          GetProcAddress(h, "AudioUtilTTS_SpeakFrom"));
//         if (speak) {
//             speak("MyPlugin", "The road to Whiterun lay open.", "");
//         }
//     }
//
// Resolving one export tells you AudioUtilTTS is present, NOT that every export is: an
// older build leaves a newer name null. Null-check the specific pointer you are about to
// call, or gate a whole group on AudioUtilTTS_GetInterfaceVersion().
//
// -------------------------------------------------------------------------------------
// SEMANTICS (identical to the Papyrus natives - same code path)
// -------------------------------------------------------------------------------------
// - `sender` is your plugin's own short name ("MyPlugin"). It is written into
//   AudioUtilTTS.log with the line, so a user can see which mod a spoken or dropped line
//   came from. Nothing else depends on it. Null = no name.
// - `text` is the line. UTF-8, or the Windows-1252 text the game's own strings use: it is
//   converted, cleaned of control characters and cut at [playback] max_chars.
// - `preset` names a voice the user defines in AudioUtilTTS.toml as [preset.<name>], or
//   one a mod brings in a file of its own, presets\<any name>.toml (0.3.2 and newer).
//   Null or "" = the default voice for narration, the speaker's own voice (by voice type)
//   for a character line. An unknown name falls back to the default voice.
// - A line is generated in the background and starts a moment later. The speak functions
//   return true when the line was queued and false when it was not: the plugin is not
//   ready or switched off, the text holds nothing speakable, or another line is in the
//   way. By default each kind of line (narration, character speech) takes one line at a
//   time: a line that arrives while another of its kind is being generated or spoken is
//   dropped and noted in the log. AudioUtilTTS_IsNarrationFree / IsSpeechFree tell
//   beforehand; they are no promise, so still check what the speak call returns.
// - When a line starts playing, the mod event AudioUtilTTS_Spoken is sent (strArg = the
//   text, numArg = the AudioUtil instance handle, sender = the speaking actor or none).
//   Sink SKSE's ModCallbackEvent source to hear it.
//
// -------------------------------------------------------------------------------------
// THREADING / LIFECYCLE
// -------------------------------------------------------------------------------------
// These are the exact code paths of the Papyrus natives, which run on Papyrus VM
// threads - call from the game thread, an SKSE task, or a VM thread. They are not
// validated from arbitrary background threads. Before kDataLoaded nothing is ready: the
// speak functions return false and so do IsReady and the two "free" queries.
//
// ABI: strings cross as null-terminated `const char*` (null tolerated = ""); actors as
// `RE::Actor*`; everything else is POD. All functions are null-safe.
//
// -------------------------------------------------------------------------------------
// WHAT THIS HEADER IS FOR (consumers)
// -------------------------------------------------------------------------------------
// It is a REFERENCE, not a link-time dependency: it documents the semantics and gives
// you the exact signatures to cast GetProcAddress results to. Copy it into your project
// and include it freely - the declarations carry no dllimport/dllexport, so including it
// can never make your plugin link against (or load-time depend on) AudioUtilTTS.
//
// Do NOT call the AudioUtilTTS_* names directly - they are declarations of functions
// that live in AudioUtilTTS's DLL, so a direct call is an unresolved external at link
// time. Always go through a function pointer obtained from GetProcAddress, as shown
// above.
// ======================================================================================

extern "C" {

// ------------------------------------------------------------------------------- Meta
// Packed AudioUtilTTS DLL version (e.g. 200002 for 0.2.2, 10000000 for 1.0.0):
// major*10000000 + minor*100000 + patch. Tracks the mod release version.
std::uint32_t AudioUtilTTS_GetVersion();
// Version of THIS C++ interface, packed MMmmpp (10000 == 1.0.0). Independent of the mod
// version; bumped only when exports are added. New functions are appended, never
// reordered or removed, so a check here is enough to feature-detect the C API surface.
std::uint32_t AudioUtilTTS_GetInterfaceVersion();

// ----------------------------------------------------------------------------- Asking
// True when the plugin can speak right now: a voice model is installed (the default one
// or any other), AudioUtil is present, a playable slot exists and speech is switched on.
// The default model is optional since 0.3.1: without it a speak call returns false for a
// line whose voice has no model of its own, so check what the call returns.
bool AudioUtilTTS_IsReady();
// Would a narration line (AudioUtilTTS_SpeakFrom) or a character line
// (AudioUtilTTS_SpeakActorFrom) sent right now be taken? Neither knows which voice the
// line will ask for. IsNarrationFree takes it to be the default voice, so it is false
// while the default model is not installed. IsSpeechFree cannot tell whether the
// speaker's voice has a model: check what AudioUtilTTS_SpeakActorFrom returns.
bool AudioUtilTTS_IsNarrationFree();
bool AudioUtilTTS_IsSpeechFree();
// True when AudioUtilTTS.toml or a preset file (presets\*.toml, 0.3.2 and newer) defines
// [preset.<preset>]. It does not tell whether the preset's model is installed.
bool AudioUtilTTS_HasPreset(const char* preset);

// --------------------------------------------------------------------------- Speaking
// Narration: flat, with no speaker.
bool AudioUtilTTS_SpeakFrom(const char* sender, const char* text, const char* preset);
// `speaker` says the line: positioned at them, with their mouth moving when `lipSync`
// is true and [speech] lipsync is on. The line is dropped if the speaker is gone or not
// loaded by the time it is ready. False for a null speaker.
bool AudioUtilTTS_SpeakActorFrom(const char* sender, RE::Actor* speaker, const char* text,
	const char* preset, bool lipSync);

// --------------------------------------------------------------------------- Stopping
// Drops every narration and character line that is waiting or being generated. The line
// already playing carries on. Dialogue lines are the game's own and are not touched.
void AudioUtilTTS_CancelPending();

}  // extern "C"
