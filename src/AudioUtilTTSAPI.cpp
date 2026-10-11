#include "API/AudioUtilTTSAPI.h"

#include "Speaker.h"

// Native C++ exports for other SKSE plugins: a mirror of the AudioUtilTTS Papyrus
// natives. Thin, null-safe forwarders onto the same Speaker functions the Papyrus
// bindings use, so C++ and Papyrus callers get identical behaviour. See
// include/API/AudioUtilTTSAPI.h for the semantics and the consumer contract.
//
// Exported by name through the linker directives below: the header a consumer copies
// stays free of dllexport, and no .def file has to be kept in step.
#pragma comment(linker, "/EXPORT:AudioUtilTTS_GetVersion")
#pragma comment(linker, "/EXPORT:AudioUtilTTS_GetInterfaceVersion")
#pragma comment(linker, "/EXPORT:AudioUtilTTS_IsReady")
#pragma comment(linker, "/EXPORT:AudioUtilTTS_IsNarrationFree")
#pragma comment(linker, "/EXPORT:AudioUtilTTS_IsSpeechFree")
#pragma comment(linker, "/EXPORT:AudioUtilTTS_HasPreset")
#pragma comment(linker, "/EXPORT:AudioUtilTTS_SpeakFrom")
#pragma comment(linker, "/EXPORT:AudioUtilTTS_SpeakActorFrom")
#pragma comment(linker, "/EXPORT:AudioUtilTTS_CancelPending")

namespace
{
	std::string_view Text(const char* a_text)
	{
		return a_text ? std::string_view{ a_text } : std::string_view{};
	}
}

extern "C" {

// ------------------------------------------------------------------------------- Meta

std::uint32_t AudioUtilTTS_GetVersion()
{
	// Packed from the DLL's own plugin declaration (xmake.lua PROJECT_VERSION), so it
	// tracks the release by itself. major*1e7 + minor*1e5 + patch (200002 = 0.2.2).
	const auto v = SKSE::PluginDeclaration::GetSingleton()->GetVersion();
	return static_cast<std::uint32_t>(v.major()) * 10000000u + static_cast<std::uint32_t>(v.minor()) * 100000u +
		   static_cast<std::uint32_t>(v.patch());
}

std::uint32_t AudioUtilTTS_GetInterfaceVersion()
{
	return 10000;  // 1.0.0 - speak with a sender name, the lane queries, cancel
}

// ----------------------------------------------------------------------------- Asking

bool AudioUtilTTS_IsReady()
{
	return Speaker::IsReady();
}

bool AudioUtilTTS_IsNarrationFree()
{
	return Speaker::IsNarrationFree();
}

bool AudioUtilTTS_IsSpeechFree()
{
	return Speaker::IsSpeechFree();
}

bool AudioUtilTTS_HasPreset(const char* preset)
{
	return Speaker::HasPreset(Text(preset));
}

// --------------------------------------------------------------------------- Speaking

bool AudioUtilTTS_SpeakFrom(const char* sender, const char* text, const char* preset)
{
	return Speaker::SpeakAs(Text(text), Text(preset), Text(sender));
}

bool AudioUtilTTS_SpeakActorFrom(const char* sender, RE::Actor* speaker, const char* text, const char* preset,
	bool lipSync)
{
	return Speaker::SpeakActor(speaker, Text(text), Text(preset), lipSync, Text(sender));
}

// --------------------------------------------------------------------------- Stopping

void AudioUtilTTS_CancelPending()
{
	Speaker::Cancel();
}

}  // extern "C"
