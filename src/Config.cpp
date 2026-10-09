#include "Config.h"

#include <toml++/toml.hpp>

namespace Config
{
	namespace
	{
		constexpr auto CONFIG_PATH = "Data\\SKSE\\Plugins\\AudioUtilTTS\\AudioUtilTTS.toml"sv;

		std::shared_ptr<const Settings> g_settings = std::make_shared<Settings>();
		std::mutex                      g_lock;
	}

	bool Load()
	{
		toml::table root;
		try {
			root = toml::parse_file(CONFIG_PATH);
		} catch (const toml::parse_error& e) {
			logger::error("Failed to parse {}: {} (line {}), keeping the previous settings", CONFIG_PATH,
				e.description(), e.source().begin.line);
			return false;
		} catch (const std::exception& e) {
			logger::error("Failed to read {}: {}, keeping the previous settings", CONFIG_PATH, e.what());
			return false;
		}

		auto settings = std::make_shared<Settings>();
		if (const auto* general = root["general"].as_table()) {
			settings->enable = (*general)["enable"].value_or(settings->enable);
		}
		if (const auto* model = root["model"].as_table()) {
			settings->model = (*model)["dir"].value_or(settings->model);
			settings->family = (*model)["family"].value_or(settings->family);
			settings->threads = (*model)["threads"].value_or(settings->threads);
			settings->lexicon = (*model)["lexicon"].value_or(settings->lexicon);
			settings->lang = (*model)["lang"].value_or(settings->lang);
		}
		if (const auto* voice = root["voice"].as_table()) {
			settings->voice = (*voice)["voice"].value_or(settings->voice);
			settings->speed = (*voice)["speed"].value_or(settings->speed);
			settings->volume = (*voice)["volume"].value_or(settings->volume);
		}
		if (const auto* playback = root["playback"].as_table()) {
			settings->group = (*playback)["group"].value_or(settings->group);
			settings->channel = (*playback)["channel"].value_or(settings->channel);
			settings->interrupt = (*playback)["interrupt"].value_or(settings->interrupt);
			settings->maxPending = (*playback)["max_pending"].value_or(settings->maxPending);
			settings->maxChars = (*playback)["max_chars"].value_or(settings->maxChars);
			settings->slots = (*playback)["slots"].value_or(settings->slots);
			settings->outputRate = (*playback)["output_rate"].value_or(settings->outputRate);
		}

		// the model name becomes part of a path: keep it to one folder name
		if (settings->model.empty() || settings->model.find_first_of("\\/:") != std::string::npos ||
			settings->model.find("..") != std::string::npos) {
			logger::warn("[model] dir = '{}' is not a plain folder name, using the default", settings->model);
			settings->model = Settings{}.model;
		}
		settings->threads = std::clamp(settings->threads, 1, 16);
		settings->speed = std::clamp(settings->speed, 0.5f, 2.0f);
		settings->volume = std::clamp(settings->volume, 0.0f, 1.0f);
		settings->maxPending = std::clamp(settings->maxPending, 1, 16);
		settings->maxChars = std::clamp(settings->maxChars, 16, 4000);
		settings->slots = std::clamp(settings->slots, 1, 16);
		settings->outputRate = settings->outputRate <= 0 ? 0 : std::clamp(settings->outputRate, 8000, 96000);

		logger::info("Config: model '{}' ({}), voice {}, speed {:.2f}, volume {:.2f}, threads {}, group '{}', "
					 "channel '{}', interrupt {}, max_pending {}, slots {}, output_rate {}{}",
			settings->model, settings->family, settings->voice, settings->speed, settings->volume,
			settings->threads, settings->group, settings->channel, settings->interrupt, settings->maxPending,
			settings->slots, settings->outputRate, settings->enable ? "" : " [disabled]");

		std::scoped_lock lock{ g_lock };
		g_settings = std::move(settings);
		return true;
	}

	std::shared_ptr<const Settings> Get()
	{
		std::scoped_lock lock{ g_lock };
		return g_settings;
	}
}
