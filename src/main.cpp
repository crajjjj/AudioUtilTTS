#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

#include "Config.h"
#include "DialogueHook.h"
#include "Greetings.h"
#include "PapyrusAPI.h"
#include "Speaker.h"
#include "menu/Menu.h"

using namespace SKSE;
using namespace SKSE::log;

namespace
{
	void InitializeLogging()
	{
		auto path = log_directory();
		if (!path) {
			stl::report_and_fail("Unable to lookup SKSE logs directory.");
		}
		*path /= PluginDeclaration::GetSingleton()->GetName();
		*path += L".log";

		std::shared_ptr<spdlog::logger> logger;
		if (IsDebuggerPresent()) {
			logger = std::make_shared<spdlog::logger>(
				"Global", std::make_shared<spdlog::sinks::msvc_sink_mt>());
		} else {
			logger = std::make_shared<spdlog::logger>(
				"Global", std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
		}
		logger->set_level(spdlog::level::info);
		logger->flush_on(spdlog::level::info);

		spdlog::set_default_logger(std::move(logger));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
	}

	void InitializePapyrus()
	{
		auto* papyrus = GetPapyrusInterface();
		if (!papyrus || !papyrus->Register(PapyrusAPI::RegisterFuncs)) {
			stl::report_and_fail("Failed to register Papyrus bindings.");
		}
		log::info("Papyrus functions bound.");
	}

	void OnMessage(MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case MessagingInterface::kDataLoaded:
			Config::Load();
			Speaker::Start();
			// the game code is only patched when the feature is wanted at launch
			if (Config::Get()->dialogue) {
				const bool hooked = DialogueHook::Install();
				Speaker::SetDialogueHooked(hooked);
				if (hooked) {
					Greetings::Start();
				}
			} else {
				log::info("Dialogue: off in the config, the game is left unpatched");
			}
			// the in-game menu, when SKSE Menu Framework is installed; it never
			// takes the plugin down with it
			try {
				Menu::Register();
			} catch (...) {
				log::error("Menu: not registered");
			}
			break;
		case MessagingInterface::kPreLoadGame:
		case MessagingInterface::kNewGame:
			// AudioUtil stops its own audio here; drop what is still on the way
			Speaker::Reset();
			Greetings::Reset();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const LoadInterface* skse)
{
	InitializeLogging();

	const auto* plugin = PluginDeclaration::GetSingleton();
	log::info("{} v{} is loading...", plugin->GetName(), plugin->GetVersion());
	log::info("Runtime version: {}", REL::Module::get().version().string());

	Init(skse);
	InitializePapyrus();
	DialogueHook::Reserve();

	if (const auto* messaging = GetMessagingInterface()) {
		messaging->RegisterListener(OnMessage);
	}

	log::info("{} loaded.", plugin->GetName());
	return true;
}
