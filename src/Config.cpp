#include "Config.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <toml++/toml.hpp>

namespace Config
{
	namespace
	{
		constexpr auto CONFIG_PATH = "Data\\SKSE\\Plugins\\AudioUtilTTS\\AudioUtilTTS.toml"sv;

		std::shared_ptr<const Settings> g_settings = std::make_shared<Settings>();
		std::mutex                      g_lock;

		// Reads every models\<folder>\model.toml (see ModelVoice), in folder name
		// order: a voice type two models list goes to the later one.
		void LoadModelFiles(Settings& a_settings)
		{
			namespace fs = std::filesystem;
			std::map<std::string, fs::path> files;  // by Normalize(folder name)
			std::error_code                 ec;
			for (const auto& entry : fs::directory_iterator(fs::path(MODELS_DIR), ec)) {
				std::error_code ignored;
				const auto      file = entry.path() / fs::path(MODEL_FILE);
				if (!fs::is_regular_file(file, ignored)) {
					continue;
				}
				try {
					// UTF-8, which is how the rest of the plugin reads a model's name
					const auto utf8 = entry.path().filename().u8string();
					auto       folder = Normalize(std::string(utf8.begin(), utf8.end()));
					if (IsFolderName(folder)) {
						files.emplace(std::move(folder), file);
					}
				} catch (const std::exception&) {
					// a name that is not valid text: that model cannot be loaded either
				}
			}

			for (const auto& [folder, file] : files) {
				toml::table root;
				try {
					// opened by its path object, so any folder name works
					std::ifstream in(file, std::ios::binary);
					if (!in) {
						throw std::runtime_error("the file could not be opened");
					}
					root = toml::parse(in);
				} catch (const toml::parse_error& e) {
					logger::error("Failed to parse {} of model '{}': {} (line {}), file ignored", MODEL_FILE, folder,
						e.description(), e.source().begin.line);
					continue;
				} catch (const std::exception& e) {
					logger::error("Failed to read {} of model '{}': {}, file ignored", MODEL_FILE, folder, e.what());
					continue;
				}

				ModelVoice base;
				base.model = folder;
				if (const auto* voice = root["voice"].as_table()) {
					base.speed = (*voice)["speed"].value_or(base.speed);
					base.volume = (*voice)["volume"].value_or(base.volume);
					if (base.speed > 0.0f) {
						base.speed = std::clamp(base.speed, 0.5f, 2.0f);
					}
					if (base.volume > 1.0f) {
						base.volume = 1.0f;
					}
				}

				std::size_t listed = 0;
				if (const auto* voiceTypes = root["voicetype"].as_table()) {
					for (auto&& [name, node] : *voiceTypes) {
						const auto         key = Normalize(name.str());
						std::optional<int> number;
						if (node.is_integer()) {
							number = node.value<int>();
						}
						if (key.empty() || !number || *number < 0) {
							logger::warn("{} of model '{}': {} must be a voice number (0 or more), ignored", MODEL_FILE,
								folder, name.str());
							continue;
						}
						if (const auto it = a_settings.modelVoiceTypes.find(key);
							it != a_settings.modelVoiceTypes.end() && it->second.model != folder) {
							logger::warn("Voice type {} is listed by model '{}' and by model '{}': '{}' is used. A "
										 "[voicetype] line in AudioUtilTTS.toml settles it.",
								name.str(), it->second.model, folder, folder);
						}
						auto voice = base;
						voice.voice = *number;
						a_settings.modelVoiceTypes[key] = std::move(voice);
						++listed;
					}
				}
				logger::info("Model '{}' lists {} voice types in its {}", folder, listed, MODEL_FILE);
			}
		}

		// The *.toml files of a folder that mods drop files into, by Normalize(file
		// name): each one's name as written and its path. A folder that is not
		// there holds none.
		using TomlFiles = std::map<std::string, std::pair<std::string, std::filesystem::path>>;

		TomlFiles TomlFilesIn(std::string_view a_dir)
		{
			namespace fs = std::filesystem;
			TomlFiles       files;
			std::error_code ec;
			for (fs::directory_iterator it(fs::path(a_dir), ec), end; !ec && it != end; it.increment(ec)) {
				std::error_code ignored;
				if (!it->is_regular_file(ignored)) {
					continue;
				}
				try {
					const auto utf8 = it->path().filename().u8string();
					auto       name = std::string(utf8.begin(), utf8.end());
					if (auto key = Normalize(name); key.ends_with(".toml")) {
						files.emplace(std::move(key), std::pair{ std::move(name), it->path() });
					}
				} catch (const std::exception&) {
					// a name that is not valid text: skipped
				}
			}
			return files;
		}

		// Parses one of those files. a_what says what it is, for the log ("the
		// voice type patch"). false = it could not be read, and the log says why.
		bool ParseTomlFile(const std::filesystem::path& a_path, std::string_view a_what, const std::string& a_name,
			toml::table& a_root)
		{
			try {
				// opened by its path object, so any file name works
				std::ifstream in(a_path, std::ios::binary);
				if (!in) {
					throw std::runtime_error("the file could not be opened");
				}
				a_root = toml::parse(in);
				return true;
			} catch (const toml::parse_error& e) {
				logger::error("Failed to parse {} {}: {} (line {}), file ignored", a_what, a_name, e.description(),
					e.source().begin.line);
			} catch (const std::exception& e) {
				logger::error("Failed to read {} {}: {}, file ignored", a_what, a_name, e.what());
			}
			return false;
		}

		// Which model folders hold a model, as far as asked: Normalize(folder name).
		using InstalledModels = std::unordered_map<std::string, bool>;

		// Is the model a patch or a preset file names installed? Only for the note
		// in the log when it is not: a line that asks for such a voice is dealt
		// with where it is spoken, since a model can be added later.
		bool ModelInstalled(const std::string& a_folder, InstalledModels& a_known)
		{
			namespace fs = std::filesystem;
			if (const auto it = a_known.find(a_folder); it != a_known.end()) {
				return it->second;
			}
			bool            found = false;
			std::error_code ec;
			const auto      dir = fs::path(MODELS_DIR) / fs::path(std::u8string(a_folder.begin(), a_folder.end()));
			for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
				std::error_code ignored;
				if (it->is_regular_file(ignored) && it->path().extension() == ".onnx") {
					found = true;
					break;
				}
			}
			a_known.emplace(a_folder, found);
			return found;
		}

		// "a, b and c", and "a, b, c and 4 more" for a long list
		std::string Listed(const std::vector<std::string>& a_names)
		{
			constexpr std::size_t MAX_NAMED = 8;
			std::string           out;
			const auto            named = std::min(a_names.size(), MAX_NAMED);
			for (std::size_t i = 0; i < named; ++i) {
				const bool last = i + 1 == a_names.size();
				out += i == 0 ? "" : (last ? " and " : ", ");
				out += a_names[i];
			}
			if (a_names.size() > named) {
				out += std::format(" and {} more", a_names.size() - named);
			}
			return out;
		}

		// The names as written, in the order of their keys
		std::vector<std::string> Values(const std::map<std::string, std::string>& a_byKey)
		{
			std::vector<std::string> out;
			for (const auto& [key, written] : a_byKey) {
				out.push_back(written);
			}
			return out;
		}

		// One [preset.<name>] table, from AudioUtilTTS.toml or from a preset file.
		// a_label names it in a warning. nullopt = it is no table.
		std::optional<Preset> ReadPreset(const toml::node& a_node, const std::string& a_label)
		{
			const auto* table = a_node.as_table();
			if (!table) {
				logger::warn("{} is not a table, ignored", a_label);
				return std::nullopt;
			}
			Preset preset;
			preset.model = (*table)["model"].value_or(""s);
			preset.voice = (*table)["voice"].value_or(-1);
			preset.speed = (*table)["speed"].value_or(0.0f);
			preset.volume = (*table)["volume"].value_or(-1.0f);
			if (!preset.model.empty() && !IsFolderName(preset.model)) {
				logger::warn("{} model = '{}' is not a plain folder name, using the default model", a_label,
					preset.model);
				preset.model.clear();
			}
			if (preset.speed > 0.0f) {
				preset.speed = std::clamp(preset.speed, 0.5f, 2.0f);
			}
			if (preset.volume > 1.0f) {
				preset.volume = 1.0f;
			}
			return preset;
		}

		// Reads every presets\*.toml (see Preset), in file name order, on top of
		// the presets of AudioUtilTTS.toml: a name the user's file defines stays
		// the user's, a name two preset files define goes to the later one.
		void LoadPresetFiles(Settings& a_settings, InstalledModels& a_installed)
		{
			for (const auto& [fileKey, file] : TomlFilesIn(PRESET_DIR)) {
				const auto& [name, path] = file;
				toml::table root;
				if (!ParseTomlFile(path, "the preset file", name, root)) {
					continue;
				}

				std::size_t                        added = 0;
				std::vector<std::string>           overruled;  // AudioUtilTTS.toml has its own
				std::map<std::string, std::string> missing;    // models nobody installed, as written
				if (const auto* presets = root["preset"].as_table()) {
					for (auto&& [presetName, node] : *presets) {
						const auto id = Normalize(presetName.str());
						auto       preset = ReadPreset(node, std::format("{}: [preset.{}]", name, presetName.str()));
						if (id.empty() || !preset) {
							continue;
						}
						if (const auto it = a_settings.presets.find(id); it != a_settings.presets.end()) {
							if (it->second.source.empty()) {
								overruled.emplace_back(presetName.str());
								continue;
							}
							if (it->second.source != name) {
								logger::info("Preset {} is defined by the preset files {} and {}: {} is used",
									presetName.str(), it->second.source, name, name);
							}
						}
						if (const auto model = Normalize(preset->model);
							!model.empty() && !ModelInstalled(model, a_installed)) {
							missing.emplace(model, preset->model);
						}
						preset->source = name;
						a_settings.presets[id] = std::move(*preset);
						++added;
					}
				}
				std::string notes;
				if (!overruled.empty()) {
					notes += std::format(". Not taken, AudioUtilTTS.toml defines its own: {}", Listed(overruled));
				}
				if (!missing.empty()) {
					notes += std::format(". Models that are not installed (their presets speak with the default "
										 "voice): {}",
						Listed(Values(missing)));
				}
				logger::info("Preset file {}: {} presets{}", name, added, notes);
			}
		}

		// Reads every voicetypes\*.toml (see Settings::patchVoiceTypes), in file
		// name order: a voice type two patches name goes to the later one.
		void LoadPatchFiles(Settings& a_settings, InstalledModels& a_installed)
		{
			for (const auto& [fileKey, file] : TomlFilesIn(PATCH_DIR)) {
				const auto& [name, path] = file;
				toml::table root;
				if (!ParseTomlFile(path, "the voice type patch", name, root)) {
					continue;
				}

				std::size_t                        given = 0;
				std::size_t                        unvoiced = 0;  // of those, lines whose model nobody installed
				std::map<std::string, std::string> missing;       // those models, as written
				if (const auto* voiceTypes = root["voicetype"].as_table()) {
					for (auto&& [type, node] : *voiceTypes) {
						const auto id = Normalize(type.str());
						ModelVoice voice;
						voice.source = name;
						std::string written;  // the model as the file spells it, for the log
						if (const auto model = node.value<std::string>()) {
							written = *model;
						} else if (const auto* table = node.as_table()) {
							written = (*table)["model"].value_or(std::string{});
							voice.voice = (*table)["voice"].value_or(voice.voice);
							voice.speed = (*table)["speed"].value_or(voice.speed);
							voice.volume = (*table)["volume"].value_or(voice.volume);
						}
						voice.model = Normalize(written);
						if (id.empty() || !IsFolderName(voice.model) || voice.voice < 0) {
							logger::warn("Voice type patch {}: {} must name a model folder, as \"folder\" or as "
										 "{{ model = \"folder\", voice = 0 }}; ignored",
								name, type.str());
							continue;
						}
						if (voice.speed > 0.0f) {
							voice.speed = std::clamp(voice.speed, 0.5f, 2.0f);
						}
						if (voice.volume > 1.0f) {
							voice.volume = 1.0f;
						}
						if (const auto it = a_settings.patchVoiceTypes.find(id);
							it != a_settings.patchVoiceTypes.end() && it->second.source != name) {
							logger::info("Voice type {} is named by the patches {} and {}: {} is used", type.str(),
								it->second.source, name, name);
						}
						if (!ModelInstalled(voice.model, a_installed)) {
							++unvoiced;
							missing.emplace(voice.model, written);
						}
						a_settings.patchVoiceTypes[id] = std::move(voice);
						++given;
					}
				}
				if (missing.empty()) {
					logger::info("Voice type patch {}: {} voice types", name, given);
				} else {
					logger::info("Voice type patch {}: {} voice types; {} of them are skipped, their voices are not "
								 "installed: {}",
						name, given, unvoiced, Listed(Values(missing)));
				}
			}
		}
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
			settings->dialogueSlots = (*dialogue)["slots"].value_or(settings->dialogueSlots);
			settings->dialogueLip = (*dialogue)["lipsync"].value_or(settings->dialogueLip);
			settings->dialogueLipStrength = (*dialogue)["lip_strength"].value_or(settings->dialogueLipStrength);
			settings->dialogueFallback = (*dialogue)["fallback"].value_or(settings->dialogueFallback);
			settings->dialoguePrepare = (*dialogue)["prepare_greetings"].value_or(settings->dialoguePrepare);
			settings->dialogueGreetings =
				(*dialogue)["greetings"].value_or(settings->dialogueGreetings);
			settings->dialogueLateMax = (*dialogue)["greetings_max_late"].value_or(settings->dialogueLateMax);
			settings->dialogueLateSplit = (*dialogue)["greetings_split"].value_or(settings->dialogueLateSplit);
			settings->dialogueSkipDirections =
				(*dialogue)["skip_directions"].value_or(settings->dialogueSkipDirections);
			settings->dialogueCacheMb = (*dialogue)["cache_max_mb"].value_or(settings->dialogueCacheMb);
		}
		settings->dialogueSlots = std::clamp(settings->dialogueSlots, 64, 256);
		settings->dialogueLipStrength = std::clamp(settings->dialogueLipStrength, 0.0f, 1.5f);
		settings->dialogueLateMax = std::clamp(settings->dialogueLateMax, 0.5f, 10.0f);
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
		settings->maxPending = std::clamp(settings->maxPending, 0, 16);
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
				const auto key = Normalize(name.str());
				auto       preset = ReadPreset(node, std::format("[preset.{}]", name.str()));
				if (key.empty() || !preset) {
					continue;
				}
				settings->presets[key] = std::move(*preset);
			}
		}
		const auto ownPresets = settings->presets.size();

		InstalledModels installed;
		LoadPresetFiles(*settings, installed);
		LoadModelFiles(*settings);
		LoadPatchFiles(*settings, installed);

		logger::info("Config: model '{}' ({}), voice {}, speed {:.2f}, volume {:.2f}, threads {}, max_loaded {}, "
					 "group '{}', channel '{}', interrupt {}, max_pending {}, slots {}, output_rate {}, {} presets "
					 "({} of them from preset files), speech group '{}', channel '{}', slots {}, lipsync {}{}",
			settings->model, settings->family, settings->voice, settings->speed, settings->volume,
			settings->threads, settings->maxLoaded, settings->group, settings->channel, settings->interrupt,
			settings->maxPending, settings->slots, settings->outputRate, settings->presets.size(),
			settings->presets.size() - ownPresets, settings->speechGroup, settings->speechChannel,
			settings->speechSlots, settings->speechLipSync, settings->enable ? "" : " [disabled]");
		logger::info("Config: dialogue {}, slots {}, lipsync {} (strength {:.2f}), fallback {}, "
					 "greetings {} (up to {:.1f} s late, in two parts {}), prepare_greetings {}, skip_directions {}, "
					 "cache {} MB, {} voice types mapped, {} given a voice by patches, {} listed by models",
			settings->dialogue, settings->dialogueSlots, settings->dialogueLip, settings->dialogueLipStrength,
			settings->dialogueFallback, settings->dialogueGreetings, settings->dialogueLateMax,
			settings->dialogueLateSplit, settings->dialoguePrepare,
			settings->dialogueSkipDirections,
			settings->dialogueCacheMb, settings->voiceTypes.size(), settings->patchVoiceTypes.size(),
			settings->modelVoiceTypes.size());

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
