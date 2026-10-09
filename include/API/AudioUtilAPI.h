#pragma once

#include <cstdint>

namespace RE
{
	class Actor;
}

// ======================================================================================
// AudioUtil - native C++ inter-plugin API
// ======================================================================================
//
// A C++ mirror of the AudioUtil Papyrus `PlayFile` natives (the canonical modder
// interface), for SKSE plugins that want to play loose audio files directly in C++ -
// same semantics as the script API, no Papyrus round-trip, no SNDR/SOUN forms.
//
// Consumers do NOT link against AudioUtil - resolve the exports at runtime:
//
//     auto h = GetModuleHandleA("AudioUtil.dll");                 // null if not installed
//     if (h) {
//         // decltype(&Fn) takes each pointer type straight from this header, so a
//         // signature change here becomes a compile error in your plugin instead of a
//         // silent mismatch. It inspects the declaration only - no link dependency.
//         auto playFile = reinterpret_cast<decltype(&AudioUtil_PlayFile)>(
//                             GetProcAddress(h, "AudioUtil_PlayFile"));
//         if (playFile) {
//             int32_t id = playFile("Sound\\FX\\MyMod\\whoosh.wav", actor, 1.0f, "", "");
//         }
//     }
//
// Resolving one export tells you AudioUtil is present, NOT that every export is: an
// older build resolves AudioUtil_PlayFile and leaves a newer name null. Null-check the
// specific pointer you are about to call, or gate a whole group on
// AudioUtil_GetInterfaceVersion().
//
// -------------------------------------------------------------------------------------
// SEMANTICS (identical to the Papyrus natives - same code path)
// -------------------------------------------------------------------------------------
// - `dataRelPath` is Data-relative ("Sound\\FX\\MyMod\\whoosh.wav"; forward slashes are
//   normalized). Playable formats: wav, xwm, fuz - loose or BSA-packed (the engine's
//   resource loader resolves both; a .fuz plays via its decoded PCM cache).
// - `follow` 3D-positions the sound at that actor (it tracks the actor while playing);
//   nullptr plays flat/2D at full volume.
// - `volume` is 0.0-1.0; effective volume = volume x group volume x duck factor.
// - `group` is a named volume group (SetGroupVolume/DuckGroup from the Papyrus side
//   affect it); "" = no special grouping.
// - `channel` is an exclusivity lane: playing on an occupied channel stops the previous
//   instance; "" = no channel.
// - Returns an instance handle: >0 success, 0 = nothing played. Handles share the id
//   space of the Papyrus API, so a handle returned here works with AudioUtil.StopHandle /
//   IsHandlePlaying / SetHandleVolume etc. from script. This C surface has no handle
//   ops of its own (yet) - feature-detect additions via AudioUtil_GetInterfaceVersion.
// - A same-named `.toml` caption sidecar next to the wav shows its text as a game
//   subtitle attributed to `follow` (loose files only), exactly like the Papyrus call.
// - The WithLipSync variant additionally drives `follow`'s mouth like a voice line:
//   authored `.lip`/fuz phoneme curves when available, else the amplitude envelope
//   (needs loose PCM wav or fuz). The global [lipsync] toggle and the gag / tongue /
//   player-dialogue guards all apply. The plain variant NEVER moves the mouth.
//
// -------------------------------------------------------------------------------------
// THREADING / LIFECYCLE
// -------------------------------------------------------------------------------------
// These are the exact code paths of the Papyrus natives, which run on Papyrus VM
// threads - call from the game thread, an SKSE task, or a VM thread. They are not
// validated from arbitrary background threads. Do not call before kDataLoaded (the
// audio engine and AudioUtil's config are not up yet).
//
// The mouth-claim group is looser, because it touches no engine state: ClaimMouth,
// ReleaseMouth, IsMouthClaimed, GetMouthClaimOwner and GetMouthClaimTimeLeft are pure
// in-memory work on AudioUtil's own state, behind AudioUtil's own mutexes, never held
// across a call into the game or the Papyrus VM - so they are safe from ANY thread (an
// audio callback, a decode worker). ClaimMouth additionally hands the mouth over by
// dropping AudioUtil's in-flight lipsync entry for that actor, which takes a second
// AudioUtil mutex; that is still no engine call, and the two locks are never nested.
// IsMouthBusy and GetClaimedActors are the exceptions: the first reads the actor facegen
// dialogue data and AudioUtil live lipsync entries, the second resolves form ids through
// the form table, so give those two the game thread / a VM thread like the rest. Claims
// are session state - they are dropped on load and on new game.
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
// can never make your plugin link against (or load-time depend on) AudioUtil.
//
// Do NOT call the AudioUtil_* names directly - they are declarations of functions that
// live in AudioUtil's DLL, so a direct call is an unresolved external at link time.
// Always go through a function pointer obtained from GetProcAddress, as shown above.
// (Inside AudioUtil itself the exports come from exports.def.)
// ======================================================================================

extern "C" {

// ------------------------------------------------------------------------------- Meta
// Packed AudioUtil DLL version (e.g. 900009 for 0.9.9, 10000000 for 1.0.0):
// major*10000000 + minor*100000 + patch. Tracks the mod release version.
std::uint32_t AudioUtil_GetVersion();
// Version of THIS C++ interface, packed MMmmpp (10000 == 1.0.0). Independent of the mod
// version; bumped only when exports are added. New functions are appended, never
// reordered/removed, so a check here is enough to feature-detect the C API surface.
std::uint32_t AudioUtil_GetInterfaceVersion();

// ---------------------------------------------------------------------------- Playback
// Play a loose (or BSA-packed) audio file by Data-relative path. Never drives the
// mouth. Returns the instance handle (>0) or 0 on failure.
std::int32_t AudioUtil_PlayFile(const char* dataRelPath, RE::Actor* follow,
	float volume, const char* group, const char* channel);
// Spoken-line variant: same as AudioUtil_PlayFile plus voice-call lipsync on `follow`
// (subject to the global lipsync toggle and the gag/tongue/dialogue guards).
std::int32_t AudioUtil_PlayFileWithLipSync(const char* dataRelPath, RE::Actor* follow,
	float volume, const char* group, const char* channel);

// ------------------------------------------------------------------------ Mouth claims
// Cross-mod jaw arbitration (interface version >= 10100). A CLAIM says "something other
// than AudioUtil is driving this actor's mouth for a spoken line right now" - for a voice
// mod that plays its own audio, so the engine never allocates dialogue data and nothing
// else can tell. It is symmetric: while a claim is live AudioUtil keeps its own lipsync
// off that mouth, and expression mods asking AudioUtil_IsMouthBusy leave the phoneme half
// of their presets alone. It is NOT an audio lock - playback, captions, ducking and volume
// groups are untouched.

// Claim `actor`'s mouth for `seconds` (the line's audio length). The claim is a DEADLINE,
// not a flag, so a sender that dies mid-line cannot strand a mouth; it is clamped to 30s,
// and a line longer than that should re-claim. `seconds` <= 0 gets 5s. `owner` is your own
// short tag ("DBReV"): it keys the claim slot, so a re-claim replaces only your claim, and
// a claim by another mod never displaces yours. Null/empty owner is a legal shared key.
// Matching is case-insensitive. No-op for a null actor.
void AudioUtil_ClaimMouth(RE::Actor* actor, float seconds, const char* owner);

// Release your claim early - a skipped line, a scene cut. Idempotent and scoped: it clears
// only the slot keyed by `owner`, never anyone else's.
void AudioUtil_ReleaseMouth(RE::Actor* actor, const char* owner);

// True while a claim on this actor is live (claims only - not engine dialogue or
// AudioUtil's own lipsync). NOTE this counts YOUR claim too: it answers "is this mouth
// claimed", not "is it claimed by someone else". A mod that both claims and queries
// already knows about its own claim - track it locally rather than asking here.
bool AudioUtil_IsMouthClaimed(RE::Actor* actor);

// The union predicate for expression / face mods - true when ANY of:
//   - the engine is speaking a line through this actor (facegen dialogue data, which is
//     what a Player.SpeakSound voice mod like DBVO produces),
//   - AudioUtil is lipsyncing it (same as the Papyrus IsLipSyncActive),
//   - a foreign claim is live.
// One call covers all three; there is no need to test them separately.
bool AudioUtil_IsMouthBusy(RE::Actor* actor);

// Diagnostics: owner tag of the live claim with the furthest deadline, written into your
// buffer (always null-terminated, truncated to fit). Returns chars written; 0 (an empty
// string) means nothing holds this mouth. A claim made with an empty owner tag reports as
// "<anonymous>", so an empty result is never an anonymous claim.
std::uint32_t AudioUtil_GetMouthClaimOwner(RE::Actor* actor, char* buffer, std::uint32_t size);

// Seconds until this actor's claim runs out; 0.0 when nothing holds the mouth.
float AudioUtil_GetMouthClaimTimeLeft(RE::Actor* actor);

// Enumerate the actors that currently hold a claim, so a listing can be built without
// parsing text. Writes at most `max` entries into `out` and ALWAYS returns the total
// number of RESOLVABLE claims - every claim whose actor form still resolves, written or
// not - so a return greater than `max` means your buffer was too small and the extras
// were not written. A claim on an actor the form table no longer resolves (unloaded or
// deleted since the claim landed) is skipped and counts toward neither, so this can read
// lower than the live-claim count `autest claims` prints. out = nullptr (with max = 0)
// is the count-only call. A fixed buffer needs one call:
//
//     RE::Actor* held[16];
//     uint32_t   total = getClaimed(held, 16);
//     uint32_t   got   = total < 16 ? total : 16;   // clamp before you iterate
//
// Claims are few (one per talking actor), so 16 is a generous buffer. Growing instead:
//
//     uint32_t n = getClaimed(nullptr, 0);
//     std::vector<RE::Actor*> held(n);
//     if (n) {
//         uint32_t total = getClaimed(held.data(), n);   // n grew? clamp, don't trust
//         held.resize(total < n ? total : n);
//     }
//
// Interface version >= 10100.
std::uint32_t AudioUtil_GetClaimedActors(RE::Actor** out, std::uint32_t max);

}  // extern "C"
