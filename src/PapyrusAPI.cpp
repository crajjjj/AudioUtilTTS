#include "PapyrusAPI.h"

#include "Speaker.h"

namespace PapyrusAPI
{
	namespace
	{
		constexpr auto         SCRIPT_NAME = "AudioUtilTTS"sv;
		constexpr std::int32_t API_VERSION = 1;

		std::int32_t GetAPIVersion(RE::StaticFunctionTag*)
		{
			return API_VERSION;
		}

		bool IsReady(RE::StaticFunctionTag*)
		{
			return Speaker::IsReady();
		}

		bool Speak(RE::StaticFunctionTag*, RE::BSFixedString a_text, std::int32_t a_voice, float a_volume,
			float a_speed)
		{
			return Speaker::Speak(a_text.c_str(), a_voice, a_volume, a_speed);
		}

		bool SpeakAs(RE::StaticFunctionTag*, RE::BSFixedString a_text, RE::BSFixedString a_preset)
		{
			return Speaker::SpeakAs(a_text.c_str(), a_preset.c_str());
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
	}

	bool RegisterFuncs(RE::BSScript::IVirtualMachine* a_vm)
	{
		REGISTERFUNC(GetAPIVersion, SCRIPT_NAME);
		REGISTERFUNC(IsReady, SCRIPT_NAME);
		REGISTERFUNC(Speak, SCRIPT_NAME);
		REGISTERFUNC(SpeakAs, SCRIPT_NAME);
		REGISTERFUNC(HasPreset, SCRIPT_NAME);
		REGISTERFUNC(CancelPending, SCRIPT_NAME);
		REGISTERFUNC(IsBusy, SCRIPT_NAME);
		REGISTERFUNC(GetVoiceCount, SCRIPT_NAME);
		REGISTERFUNC(SetEnabled, SCRIPT_NAME);
		REGISTERFUNC(IsEnabled, SCRIPT_NAME);
		REGISTERFUNC(ReloadConfig, SCRIPT_NAME);
		REGISTERFUNC(GetStatus, SCRIPT_NAME);
		REGISTERFUNC(GetChannel, SCRIPT_NAME);
		return true;
	}
}
