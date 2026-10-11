#include "DialogueHook.h"

#include "Speaker.h"

#include <cstring>

namespace DialogueHook
{
	namespace
	{
		constexpr std::size_t PATH_BUFFER = 0x104;  // the game's own buffer for the voice file path

		// The subtitle of the response being built on this thread, handed from the
		// first call site to the second. A copy, taken and cleared in one go: a
		// response that skips the first site can never pick up another one's text.
		thread_local std::string t_subtitle;

		// See Preparing: set while this thread builds responses only to learn them.
		thread_local bool t_preparing = false;
		thread_local int  t_seen = 0;
		thread_local int  t_queued = 0;

		// "Data\Sound\Voice\Plugin.esp\VoiceType\Topic_0001ABCD_1.wav": is there a
		// voice file for it, in any of the three forms the game accepts? Asked
		// through the game's own resource lookup, so archives count.
		bool HasVoiceFile(const char* a_path)
		{
			std::string path = a_path;
			if (path.size() >= 5 && _strnicmp(path.c_str(), "data\\", 5) == 0) {
				path.erase(0, 5);
			}
			const auto exists = [](const std::string& a_file) {
				RE::BSResourceNiBinaryStream stream{ a_file.c_str() };
				return stream.good();
			};
			const auto dot = path.rfind('.');
			if (dot == std::string::npos || path.find('\\', dot) != std::string::npos) {
				return exists(path);
			}
			const auto stem = path.substr(0, dot);
			return exists(stem + ".fuz") || exists(stem + ".wav") || exists(stem + ".xwm");
		}

		// The text now stored in a response (its first member, a string whose
		// first field is the character pointer). Read under a structured exception
		// guard and with a length cap: this is the one place that trusts the
		// shape of an object the game passed, and a wrong guess must cost a
		// missing line, never a crash.
		std::size_t CopyResponseText(const RE::DialogueResponse* a_response, char* a_out, std::size_t a_cap) noexcept
		{
			__try {
				const char* text = a_response ? *reinterpret_cast<const char* const*>(a_response) : nullptr;
				if (!text) {
					return 0;
				}
				std::size_t n = 0;
				while (n + 1 < a_cap && text[n]) {
					a_out[n] = text[n];
					++n;
				}
				a_out[n] = '\0';
				return n;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return 0;
			}
		}

		struct SetSubtitle
		{
			static char* thunk(RE::DialogueResponse* a_response, char* a_text, std::int32_t a_arg3)
			{
				const auto result = func(a_response, a_text, a_arg3);
				// after the call: what is in the response now is what will be
				// shown, whatever another mod made of the text on the way
				char shown[2048];
				if (CopyResponseText(a_response, shown, sizeof(shown)) > 0) {
					t_subtitle = shown;
				} else {
					t_subtitle = a_text ? a_text : "";
				}
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BuildVoicePath
		{
			static bool thunk(RE::TESTopicInfo::TESResponse* a_response, char* a_path, RE::BGSVoiceType* a_voiceType,
				RE::TESTopic* a_topic, RE::TESTopicInfo* a_info)
			{
				std::string text;
				text.swap(t_subtitle);

				const bool built = func(a_response, a_path, a_voiceType, a_topic, a_info);
				if (!built || !a_response || !a_path || !a_voiceType || !Speaker::IsDialogueEnabled()) {
					return built;
				}
				// a response that plays a sound of its own has no voice file by design
				if (a_response->sound || std::strlen(a_path) < 17) {
					return built;
				}
				if (text.empty()) {
					if (const char* raw = a_response->responseText.c_str()) {
						text = raw;
					}
				}
				if (text.empty() || HasVoiceFile(a_path)) {
					return built;
				}
				if (t_preparing) {
					// not about to be said: generated for the cache, the path stays
					++t_seen;
					if (Speaker::PrepareDialogue(text, a_voiceType, a_response->responseNumber, a_info)) {
						++t_queued;
					}
					return built;
				}
				const auto voiced = Speaker::VoiceDialogue(text, a_voiceType, a_response->responseNumber, a_info);
				if (!voiced.empty() && voiced.size() < PATH_BUFFER) {
					std::memcpy(a_path, voiced.c_str(), voiced.size() + 1);
				}
				return built;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	Preparing::Preparing()
	{
		t_preparing = true;
		t_seen = 0;
		t_queued = 0;
	}

	Preparing::~Preparing()
	{
		t_preparing = false;
	}

	int Preparing::Seen() const
	{
		return t_seen;
	}

	int Preparing::Queued() const
	{
		return t_queued;
	}

	void Reserve()
	{
		SKSE::AllocTrampoline(64);
	}

	bool Install()
	{
		// the response builder; the third number is its place in the VR runtime
		const REL::Relocation<std::uintptr_t> builder{ REL::VariantID(34429, 35249, 0x573B70) };
		const auto                            subtitleCall = builder.address() + 0x61;
		const auto                            pathCall = builder.address() + 0xDE;

		// Both sites must be direct calls (E8). They are in every runtime this was
		// checked against; if a future one moves them, nothing is patched.
		const auto isCall = [](std::uintptr_t a_at) { return *reinterpret_cast<const std::uint8_t*>(a_at) == 0xE8; };
		if (!isCall(subtitleCall) || !isCall(pathCall)) {
			logger::error("Dialogue: the game code is not what this version expects (bytes {:02X} and {:02X} at the "
						  "two call sites, E8 wanted). Unvoiced dialogue stays unvoiced; the rest works.",
				*reinterpret_cast<const std::uint8_t*>(subtitleCall), *reinterpret_cast<const std::uint8_t*>(pathCall));
			return false;
		}

		auto& trampoline = SKSE::GetTrampoline();
		SetSubtitle::func = trampoline.write_call<5>(subtitleCall, SetSubtitle::thunk);
		BuildVoicePath::func = trampoline.write_call<5>(pathCall, BuildVoicePath::thunk);
		logger::info("Dialogue: hook installed, lines without a recorded voice are generated");
		return true;
	}
}
