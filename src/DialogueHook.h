#pragma once

// Gives a voice to game dialogue that has none.
//
// When the game builds a dialogue response it works out the path of that
// response's voice file from the speaker's voice type, the topic and the
// response number. This hooks that step: if no file exists at the path (as a
// loose file or in an archive, as .fuz, .wav or .xwm), Speaker::VoiceDialogue
// is asked for the line and the path is swapped for the placeholder file that
// holds it. A response with a recorded voice is never touched.
//
// Two call sites inside the response builder (Address Library 34429 SE / 35249
// AE) are patched, the same two Dynamic Dialogue Replacer uses:
//   +0x61  sets the subtitle text       read after the call: the final text
//   +0xDE  builds the voice file path   checked and, if missing, swapped
// Both are plain 5-byte calls, patched by redirecting the call and chaining to
// whatever was there, so this works with Dynamic Dialogue Replacer in either
// load order. Fuz Ro D'oh patches a different instruction (+0xEC) and runs
// after this: it sees the swapped path, finds a file there and leaves the line
// alone, and it still covers every line whose path was not swapped.
namespace DialogueHook
{
	// The trampoline space the patch needs, reserved by SKSE::Init in SKSEPluginLoad.
	inline constexpr std::size_t kTrampolineSize = 64;

	// kDataLoaded, once. false = the patch was not applied (the bytes at the
	// call sites are not what was expected, so nothing was written).
	bool Install();

	// While one of these lives on a thread, the responses that thread builds are
	// not about to be said: they are built to learn what a speaker would say
	// (Greetings). The hook then leaves their voice file paths alone and hands
	// each unvoiced one to Speaker::PrepareDialogue, for the cache only.
	class Preparing
	{
	public:
		Preparing();
		~Preparing();
		Preparing(const Preparing&) = delete;
		Preparing& operator=(const Preparing&) = delete;

		[[nodiscard]] int Seen() const;    // unvoiced responses built so far
		[[nodiscard]] int Queued() const;  // of those, lines queued for generation
	};
}
