#include "DBReV.h"

#include "Config.h"
#include "Speaker.h"

#include <API/DBReV_API.h>

#include <Windows.h>

namespace DBReV
{
	namespace
	{
		constexpr auto PROVIDER_NAME = "AudioUtilTTS";

		std::atomic<::DBReV::VoiceLineStartedFn> g_lineStarted{ nullptr };
		std::atomic<::DBReV::VoiceLineFailedFn>  g_lineFailed{ nullptr };
		std::atomic<bool>                        g_present{ false };
		std::atomic<bool>                        g_listening{ false };
		std::atomic<bool>                        g_registered{ false };
		std::atomic<bool>                        g_tooOld{ false };  // DBReV is there but predates providers
		std::atomic<float>                       g_registeredWait{ 0.0f };

		// The line DBReV is holding the conversation for, 0 = none. Set before
		// the line is handed to Speaker, since its answer can come at once;
		// cleared when it starts, fails, ends or is declined. The callbacks
		// come on the game thread, the worker and whichever thread drops a
		// line, hence the lock.
		std::mutex    g_lock;
		std::uint32_t g_held = 0;

		std::atomic<std::uint32_t> g_offered{ 0 };
		std::atomic<std::uint32_t> g_taken{ 0 };
		std::atomic<std::uint32_t> g_declined{ 0 };
		std::atomic<std::uint32_t> g_failed{ 0 };
		std::atomic<std::uint32_t> g_endedEarly{ 0 };
		std::atomic<bool>          g_loggedPack{ false };

		// Takes the slot for a_id, so that only one answer about it reaches
		// DBReV. false = DBReV is not waiting for that line any more.
		bool Release(std::uint32_t a_id)
		{
			std::scoped_lock lock{ g_lock };
			if (g_held != a_id) {
				return false;
			}
			g_held = 0;
			return true;
		}

		// The audio for a_id plays now, a_seconds long. Game thread.
		bool StartLine(std::uint32_t a_id, float a_seconds)
		{
			const auto started = g_lineStarted.load();
			if (!started || !Release(a_id)) {
				return false;
			}
			const bool wanted = started(a_id, a_seconds);
			if (!wanted) {
				g_endedEarly.fetch_add(1);
				logger::info("DBReV line {}: the conversation had moved on before the line started, it is stopped", a_id);
			}
			return wanted;
		}

		// The line will not be spoken. Any thread.
		void FailLine(std::uint32_t a_id)
		{
			const auto failed = g_lineFailed.load();
			if (!failed || !Release(a_id)) {
				return;
			}
			g_failed.fetch_add(1);
			failed(a_id);
			logger::info("DBReV line {}: given back, it could not be spoken (see above)", a_id);
		}

		// Main thread; the game waits on this call. The active voice pack has no
		// recording for the line.
		bool OnLineMissing(const ::DBReV::MissingLine* a_line)
		{
			try {
				if (!a_line || a_line->version < 1 || a_line->lineId == 0) {
					return false;
				}
				g_offered.fetch_add(1);
				const std::uint32_t id = a_line->lineId;
				const std::string   text = a_line->text ? a_line->text : "";
				if (!g_loggedPack.exchange(true)) {
					logger::info("DBReV offers the player lines its voice pack '{}' has no recording for; the first "
								 "one it looked for was '{}'",
						a_line->voicePackId ? a_line->voicePackId : "", a_line->missingPath ? a_line->missingPath : "");
				}

				Speaker::PlayerLine line;
				line.id = id;
				line.holder = "DBReV";
				line.maxWaitSeconds = a_line->maxWaitSeconds > 0.0f ? a_line->maxWaitSeconds : 10.0f;
				line.started = [id](float a_seconds) { return StartLine(id, a_seconds); };
				line.dropped = [id] { FailLine(id); };
				{
					std::scoped_lock lock{ g_lock };
					g_held = id;
				}
				const auto why = Speaker::SayPlayerLine(text, std::move(line));
				if (!why.empty()) {
					Release(id);
					g_declined.fetch_add(1);
					logger::info("DBReV line {} ({} chars) declined: {}", id, text.size(), why);
					return false;
				}
				g_taken.fetch_add(1);
				return true;
			} catch (...) {
				// neither callback may throw: the line is DBReV's to handle as a
				// line with no file
				return false;
			}
		}

		const char* ReasonText(std::uint32_t a_reason)
		{
			switch (a_reason) {
			case ::DBReV::kEndReason_Completed:
				return "completed";
			case ::DBReV::kEndReason_Skipped:
				return "skipped by the player";
			case ::DBReV::kEndReason_Superseded:
				return "another topic was picked";
			case ::DBReV::kEndReason_TimedOut:
				return "not ready in time";
			case ::DBReV::kEndReason_Cancelled:
				return "the dialogue closed";
			default:
				return "ended";
			}
		}

		// Main thread, once per line taken: DBReV has stopped holding it.
		void OnLineEnded(std::uint32_t a_id, std::uint32_t a_reason)
		{
			try {
				Release(a_id);
				const bool early = a_reason != ::DBReV::kEndReason_Completed;
				if (early) {
					g_endedEarly.fetch_add(1);
					logger::info("DBReV line {} ended early: {}", a_id, ReasonText(a_reason));
				}
				// stops the line where it still plays, drops it where it waits; a
				// completed line has played to its end
				Speaker::EndPlayerLine(a_id, early);
			} catch (...) {
			}
		}

		// DBReV announces a player line, its own or ours. Main thread since DBReV
		// 1.5.0; the two calls below touch nothing of the game's.
		void OnDBReVMessage(SKSE::MessagingInterface::Message* a_msg)
		{
			if (a_msg && a_msg->type == ::DBReV::kPlayerLineStart) {
				Speaker::PlayerSpeaks();
			}
		}

		// Game thread.
		void RegisterNow()
		{
			const auto module = GetModuleHandleA(::DBReV::kModuleName);
			if (!module) {
				return;
			}
			const auto version = reinterpret_cast<::DBReV::GetInterfaceVersionFn>(
				GetProcAddress(module, ::DBReV::kExport_GetInterfaceVersion));
			const auto reg = reinterpret_cast<::DBReV::RegisterVoiceProviderFn>(
				GetProcAddress(module, ::DBReV::kExport_RegisterVoiceProvider));
			const auto started = reinterpret_cast<::DBReV::VoiceLineStartedFn>(
				GetProcAddress(module, ::DBReV::kExport_VoiceLineStarted));
			const auto failed = reinterpret_cast<::DBReV::VoiceLineFailedFn>(
				GetProcAddress(module, ::DBReV::kExport_VoiceLineFailed));
			if (!version || version() < ::DBReV::kInterfaceVersion || !reg || !started || !failed) {
				if (!g_tooOld.exchange(true)) {
					logger::info("DBReV is installed, but this version has no voice provider API: the player's lines "
								 "without a recording stay silent");
				}
				return;
			}
			g_lineStarted.store(started);
			g_lineFailed.store(failed);

			const auto settings = Config::Get();
			::DBReV::VoiceProvider provider{};
			provider.version = 1;
			provider.structSize = sizeof(provider);
			provider.name = PROVIDER_NAME;
			provider.onLineMissing = &OnLineMissing;
			provider.onLineEnded = &OnLineEnded;
			provider.maxWaitSeconds = settings->playerMaxWait;
			const bool ok = reg(&provider);
			if (ok) {
				const bool again = g_registered.exchange(true);
				g_registeredWait.store(settings->playerMaxWait);
				logger::info("DBReV: registered as its voice provider{}: the player's lines its voice pack has no "
							 "recording for are spoken here, with up to {:.1f} s to start each",
					again ? " again" : "", settings->playerMaxWait);
			} else {
				logger::warn("DBReV refused the voice provider registration: another provider is registered (see "
							 "DBReV.log)");
			}
		}
	}

	void Listen()
	{
		const auto module = GetModuleHandleA(::DBReV::kModuleName);
		g_present.store(module != nullptr);
		if (!module) {
			return;
		}
		if (const auto* messaging = SKSE::GetMessagingInterface()) {
			g_listening.store(messaging->RegisterListener(::DBReV::kSender, OnDBReVMessage));
		}
		logger::info("DBReV found{}", g_listening.load() ? ", listening for the player's lines" : "");
	}

	void Register()
	{
		if (!g_present.load() && !GetModuleHandleA(::DBReV::kModuleName)) {
			return;
		}
		g_present.store(true);
		if (!Config::Get()->player) {
			return;  // once registered it stays registered; Speaker declines every line then
		}
		if (auto* task = SKSE::GetTaskInterface()) {
			task->AddTask([] { RegisterNow(); });
		}
	}

	bool IsPresent()
	{
		return g_present.load();
	}

	bool IsRegistered()
	{
		return g_registered.load();
	}

	std::string Status()
	{
		if (!g_present.load()) {
			return "DBReV is not installed";
		}
		if (g_tooOld.load()) {
			return "DBReV is installed, but without the voice provider API";
		}
		if (!g_registered.load()) {
			return "DBReV is installed, this plugin is not registered as its voice provider";
		}
		return std::format("DBReV voice provider (up to {:.1f} s to start a line) | lines offered {}, taken {}, declined "
						   "{}, given back {}, ended early {}",
			g_registeredWait.load(), g_offered.load(), g_taken.load(), g_declined.load(), g_failed.load(),
			g_endedEarly.load());
	}
}
