#include "PapyrusAPI.h"

#include "Speaker.h"

namespace PapyrusAPI
{
	namespace
	{
		constexpr auto         SCRIPT_NAME = "AudioUtilTTS"sv;
		// 2 added the dialogue group and GetActorVoice; 3 SpeakFrom, SpeakActorFrom,
		// IsNarrationFree and IsSpeechFree; 4 the player line group
		constexpr std::int32_t API_VERSION = 4;

		std::int32_t GetAPIVersion(RE::StaticFunctionTag*)
		{
			return API_VERSION;
		}

		bool IsReady(RE::StaticFunctionTag*)
		{
			return Speaker::IsReady();
		}

		// Speak, SpeakAs and SpeakActor are deprecated since API 3: they carry no
		// sender name. Kept, unchanged, for scripts compiled against them.
		bool Speak(RE::StaticFunctionTag*, RE::BSFixedString a_text, std::int32_t a_voice, float a_volume,
			float a_speed)
		{
			return Speaker::Speak(a_text.c_str(), a_voice, a_volume, a_speed);
		}

		bool SpeakAs(RE::StaticFunctionTag*, RE::BSFixedString a_text, RE::BSFixedString a_preset)
		{
			return Speaker::SpeakAs(a_text.c_str(), a_preset.c_str());
		}

		bool SpeakActor(RE::StaticFunctionTag*, RE::Actor* a_speaker, RE::BSFixedString a_text,
			RE::BSFixedString a_preset, bool a_lipSync)
		{
			return Speaker::SpeakActor(a_speaker, a_text.c_str(), a_preset.c_str(), a_lipSync);
		}

		// The two below replace them: the same lines with the sender's name. They
		// are natives of their own because a parameter added to an existing one
		// would fail every call from a script compiled before it ("incorrect
		// number of arguments").
		bool SpeakFrom(RE::StaticFunctionTag*, RE::BSFixedString a_sender, RE::BSFixedString a_text,
			RE::BSFixedString a_preset)
		{
			return Speaker::SpeakAs(a_text.c_str(), a_preset.c_str(), a_sender.c_str());
		}

		bool SpeakActorFrom(RE::StaticFunctionTag*, RE::BSFixedString a_sender, RE::Actor* a_speaker,
			RE::BSFixedString a_text, RE::BSFixedString a_preset, bool a_lipSync)
		{
			return Speaker::SpeakActor(a_speaker, a_text.c_str(), a_preset.c_str(), a_lipSync, a_sender.c_str());
		}

		bool HasPreset(RE::StaticFunctionTag*, RE::BSFixedString a_preset)
		{
			return Speaker::HasPreset(a_preset.c_str());
		}

		void CancelPending(RE::StaticFunctionTag*)
		{
			Speaker::Cancel();
		}

		bool IsBusy(RE::StaticFunctionTag*)
		{
			return Speaker::IsBusy();
		}

		bool IsNarrationFree(RE::StaticFunctionTag*)
		{
			return Speaker::IsNarrationFree();
		}

		bool IsSpeechFree(RE::StaticFunctionTag*)
		{
			return Speaker::IsSpeechFree();
		}

		std::int32_t GetVoiceCount(RE::StaticFunctionTag*)
		{
			return Speaker::VoiceCount();
		}

		void SetEnabled(RE::StaticFunctionTag*, bool a_enabled)
		{
			Speaker::SetEnabled(a_enabled);
		}

		bool IsEnabled(RE::StaticFunctionTag*)
		{
			return Speaker::IsEnabled();
		}

		bool ReloadConfig(RE::StaticFunctionTag*)
		{
			return Speaker::Reload();
		}

		RE::BSFixedString GetStatus(RE::StaticFunctionTag*)
		{
			return Speaker::Status();
		}

		RE::BSFixedString GetChannel(RE::StaticFunctionTag*)
		{
			return Speaker::Channel();
		}

		RE::BSFixedString GetSpeechChannel(RE::StaticFunctionTag*)
		{
			return Speaker::SpeechChannel();
		}

		void SetDialogueEnabled(RE::StaticFunctionTag*, bool a_enabled)
		{
			Speaker::SetDialogueEnabled(a_enabled);
		}

		bool IsDialogueEnabled(RE::StaticFunctionTag*)
		{
			return Speaker::IsDialogueEnabled();
		}

		RE::BSFixedString GetDialogueStatus(RE::StaticFunctionTag*)
		{
			return Speaker::DialogueStatus();
		}

		std::int32_t ClearDialogueCache(RE::StaticFunctionTag*)
		{
			return Speaker::ClearDialogueCache();
		}

		RE::BSFixedString GetActorVoice(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			return Speaker::DescribeVoice(a_actor);
		}

		// the player's own lines, with Dragonborn ReVoiced (API 4)
		void SetPlayerEnabled(RE::StaticFunctionTag*, bool a_enabled)
		{
			Speaker::SetPlayerEnabled(a_enabled);
		}

		bool IsPlayerEnabled(RE::StaticFunctionTag*)
		{
			return Speaker::IsPlayerEnabled();
		}

		RE::BSFixedString GetPlayerStatus(RE::StaticFunctionTag*)
		{
			return Speaker::PlayerStatus();
		}
	}

	bool RegisterFuncs(RE::BSScript::IVirtualMachine* a_vm)
	{
		REGISTERFUNC(GetAPIVersion, SCRIPT_NAME);
		REGISTERFUNC(IsReady, SCRIPT_NAME);
		REGISTERFUNC(Speak, SCRIPT_NAME);
		REGISTERFUNC(SpeakAs, SCRIPT_NAME);
		REGISTERFUNC(SpeakActor, SCRIPT_NAME);
		REGISTERFUNC(HasPreset, SCRIPT_NAME);
		REGISTERFUNC(CancelPending, SCRIPT_NAME);
		REGISTERFUNC(IsBusy, SCRIPT_NAME);
		REGISTERFUNC(GetVoiceCount, SCRIPT_NAME);
		REGISTERFUNC(SetEnabled, SCRIPT_NAME);
		REGISTERFUNC(IsEnabled, SCRIPT_NAME);
		REGISTERFUNC(ReloadConfig, SCRIPT_NAME);
		REGISTERFUNC(GetStatus, SCRIPT_NAME);
		REGISTERFUNC(GetChannel, SCRIPT_NAME);
		REGISTERFUNC(GetSpeechChannel, SCRIPT_NAME);
		REGISTERFUNC(SetDialogueEnabled, SCRIPT_NAME);
		REGISTERFUNC(IsDialogueEnabled, SCRIPT_NAME);
		REGISTERFUNC(GetDialogueStatus, SCRIPT_NAME);
		REGISTERFUNC(ClearDialogueCache, SCRIPT_NAME);
		REGISTERFUNC(GetActorVoice, SCRIPT_NAME);
		REGISTERFUNC(SpeakFrom, SCRIPT_NAME);
		REGISTERFUNC(SpeakActorFrom, SCRIPT_NAME);
		REGISTERFUNC(IsNarrationFree, SCRIPT_NAME);
		REGISTERFUNC(IsSpeechFree, SCRIPT_NAME);
		REGISTERFUNC(SetPlayerEnabled, SCRIPT_NAME);
		REGISTERFUNC(IsPlayerEnabled, SCRIPT_NAME);
		REGISTERFUNC(GetPlayerStatus, SCRIPT_NAME);
		return true;
	}
}
