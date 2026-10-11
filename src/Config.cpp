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

	bool IsFolderName(const std::string& a_name)
	{
		return !a_name.empty() && a_name.find_first_of("\\/:") == std::string::npos &&
			   a_name.find("..") == std::string::npos;
	}

	std::string Normalize(std::string_view a_text)
	{
		const auto first = a_text.find_first_not_of(" \t");
		if (first == std::string_view::npos) {
			return {};
		}
		const auto  last = a_text.find_last_not_of(" \t");
		std::string out(a_text.substr(first, last - first + 1));
		std::transform(out.begin(), out.end(), out.begin(),
			[](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
		return out;
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
			settings->maxLoaded = (*model)["max_loaded"].value_or(settings->maxLoaded);
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

		if (const auto* speech = root["speech"].as_table()) {
			settings->speechGroup = (*speech)["group"].value_or(settings->speechGroup);
			settings->speechChannel = (*speech)["channel"].value_or(settings->speechChannel);
			settings->speechSlots = (*speech)["slots"].value_or(settings->speechSlots);
			settings->speechLipSync = (*speech)["lipsync"].value_or(settings->speechLipSync);
		}

		if (const auto* dialogue = root["dialogue"].as_table()) {
			settings->dialogue = (*dialogue)["enable"].value_or(settings->dialogue);
			settings->dialogueWaitMs = (*dialogue)["wait_ms"].value_or(settings->dialogueWaitMs);
			settings->dialogueSlots = (*dialogue)["slots"].value_or(settings->dialogueSlots);
			settings->dialogueLip = (*dialogue)["lipsync"].value_or(settings->dialogueLip);
			settings->dialogueLipStrength = (*dialogue)["lip_strength"].value_or(settings->dialogueLipStrength);
			settings->dialogueFallback = (*dialogue)["fallback"].value_or(settings->dialogueFallback);
			settings->dialoguePrepare = (*dialogue)["prepare_greetings"].value_or(settings->dialoguePrepare);
			settings->dialogueSkipDirections =
				(*dialogue)["skip_directions"].value_or(settings->dialogueSkipDirections);
			settings->dialogueCacheMb = (*dialogue)["cache_max_mb"].value_or(settings->dialogueCacheMb);
		}
		settings->dialogueWaitMs = std::clamp(settings->dialogueWaitMs, 0, 2000);
		settings->dialogueSlots = std::clamp(settings->dialogueSlots, 64, 256);
		settings->dialogueLipStrength = std::clamp(settings->dialogueLipStrength, 0.0f, 1.5f);
		settings->dialogueCacheMb = std::clamp(settings->dialogueCacheMb, 0, 16384);

		if (const auto* voiceTypes = root["voicetype"].as_table()) {
			for (auto&& [name, node] : *voiceTypes) {
				const auto key = Normalize(name.str());
				const auto preset = node.value<std::string>();
				if (key.empty() || !preset) {
					logger::warn("[voicetype] {} must be a preset name in quotes, ignored", name.str());
					continue;
				}
				settings->voiceTypes[key] = Normalize(*preset);
			}
		}

		if (!IsFolderName(settings->model)) {
			logger::warn("[model] dir = '{}' is not a plain folder name, using the default", settings->model);
			settings->model = Settings{}.model;
		}
		settings->threads = std::clamp(settings->threads, 1, 16);
		settings->maxLoaded = std::clamp(settings->maxLoaded, 1, 32);
		settings->speed = std::clamp(settings->speed, 0.5f, 2.0f);
		settings->volume = std::clamp(settings->volume, 0.0f, 1.0f);
		settings->maxPending = std::clamp(settings->maxPending, 1, 16);
		settings->maxChars = std::clamp(settings->maxChars, 16, 4000);
		settings->slots = std::clamp(settings->slots, 1, 16);
		settings->speechSlots = std::clamp(settings->speechSlots, 1, 16);
		if (Normalize(settings->speechChannel) == Normalize(settings->channel)) {
			// the two lanes must not share a channel: each relies on its own channel
			// to have stopped a line before that line's file is rewritten
			logger::warn("[speech] channel must differ from [playback] channel, using 'speech'");
			settings->speechChannel = Normalize(settings->channel) == "speech" ? "speech2" : "speech";
		}
		settings->outputRate = settings->outputRate <= 0 ? 0 : std::clamp(settings->outputRate, 8000, 96000);

		if (const auto* presets = root["preset"].as_table()) {
			for (auto&& [name, node] : *presets) {
				const auto  key = Normalize(name.str());
				const auto* table = node.as_table();
				if (!table || key.empty()) {
					logger::warn("[preset.{}] is not a table, ignored", name.str());
					continue;
				}
				Preset preset;
				preset.model = (*table)["model"].value_or(""s);
				preset.voice = (*table)["voice"].value_or(-1);
				preset.speed = (*table)["speed"].value_or(0.0f);
				preset.volume = (*table)["volume"].value_or(-1.0f);
				if (!preset.model.empty() && !IsFolderName(preset.model)) {
					logger::warn("[preset.{}] model = '{}' is not a plain folder name, using the default model", key,
						preset.model);
					preset.model.clear();
				}
				if (preset.speed > 0.0f) {
					preset.speed = std::clamp(preset.speed, 0.5f, 2.0f);
				}
				if (preset.volume > 1.0f) {
					preset.volume = 1.0f;
				}
				settings->presets[key] = std::move(preset);
			}
		}

		logger::info("Config: model '{}' ({}), voice {}, speed {:.2f}, volume {:.2f}, threads {}, max_loaded {}, "
					 "group '{}', channel '{}', interrupt {}, max_pending {}, slots {}, output_rate {}, {} presets, "
					 "speech group '{}', channel '{}', slots {}, lipsync {}{}",
			settings->model, settings->family, settings->voice, settings->speed, settings->volume,
			settings->threads, settings->maxLoaded, settings->group, settings->channel, settings->interrupt,
			settings->maxPending, settings->slots, settings->outputRate, settings->presets.size(),
			settings->speechGroup, settings->speechChannel, settings->speechSlots, settings->speechLipSync,
			settings->enable ? "" : " [disabled]");
		logger::info("Config: dialogue {}, wait {} ms, slots {}, lipsync {} (strength {:.2f}), fallback {}, "
					 "prepare_greetings {}, skip_directions {}, cache {} MB, {} voice types mapped",
			settings->dialogue, settings->dialogueWaitMs, settings->dialogueSlots, settings->dialogueLip,
			settings->dialogueLipStrength, settings->dialogueFallback, settings->dialoguePrepare,
			settings->dialogueSkipDirections, settings->dialogueCacheMb, settings->voiceTypes.size());

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
