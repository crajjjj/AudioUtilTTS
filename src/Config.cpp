#include "Config.h"

#include "TtsEngine.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <toml++/toml.hpp>

namespace Config
{
	namespace
	{
		constexpr auto CONFIG_PATH = "Data\\SKSE\\Plugins\\AudioUtilTTS\\AudioUtilTTS.toml"sv;

		std::shared_ptr<const Settings> g_settings = std::make_shared<Settings>();
		std::mutex                      g_lock;

		// A speed or a volume as a file gives it, brought into range. What is not
		// a usable number (a NaN is valid TOML) counts as not set.
		float SaneSpeed(float a_speed)
		{
			return a_speed > 0.0f ? std::clamp(a_speed, 0.5f, 2.0f) : 0.0f;
		}

		float SaneVolume(float a_volume)
		{
			return std::isnan(a_volume) ? -1.0f : std::min(a_volume, 1.0f);
		}

		// Parses a file that is not the main config. a_what says what it is, for
		// the log ("the voice type patch"). false = it could not be read, and the
		// log says why.
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

		// What is installed, as far as asked, by Normalize(folder name): whether a
		// model folder holds a model (the same test a spoken line meets later,
		// TtsEngine::ModelStamp) and, for a Pocket model, its recordings. Used for
		// the notes in the log, and so that a voice nobody installed does not take
		// the place of one that is there.
		struct Installed
		{
			std::unordered_map<std::string, bool>                          models;
			std::unordered_map<std::string, std::vector<TtsEngine::Clip>> clips;
		};

		bool ModelInstalled(const std::string& a_folder, Installed& a_known)
		{
			if (const auto it = a_known.models.find(a_folder); it != a_known.models.end()) {
				return it->second;
			}
			const bool found = IsFolderName(a_folder) && TtsEngine::ModelStamp(std::string(MODELS_DIR) + a_folder) != 0;
			a_known.models.emplace(a_folder, found);
			return found;
		}

		// Is this voice of the model there: the model, and for a recording given
		// by name (or by a number on a Pocket model) the recording too.
		bool VoiceInstalled(const std::string& a_folder, int a_number, const std::string& a_clip, Installed& a_known)
		{
			if (!ModelInstalled(a_folder, a_known)) {
				return false;
			}
			auto it = a_known.clips.find(a_folder);
			if (it == a_known.clips.end()) {
				it = a_known.clips.emplace(a_folder, TtsEngine::Clips(std::string(MODELS_DIR) + a_folder)).first;
			}
			const auto& clips = it->second;
			if (!a_clip.empty()) {
				const auto wanted = Normalize(a_clip);
				return std::any_of(clips.begin(), clips.end(),
					[&](const TtsEngine::Clip& a_c) { return Normalize(a_c.name) == wanted; });
			}
			return clips.empty() || a_number < static_cast<int>(clips.size());
		}

		// "voice 3", "recording 'gruff'"
		std::string VoiceLabel(int a_number, const std::string& a_clip)
		{
			return a_clip.empty() ? std::format("voice {}", a_number) : std::format("recording '{}'", a_clip);
		}

		// A voice as a file gives it: a number (0 or more), or in quotes the name
		// of a Pocket model's recording. a_label names the line in a warning.
		// false = neither, and the warning is in the log.
		bool ReadVoice(const toml::node& a_node, int& a_number, std::string& a_clip, const std::string& a_label)
		{
			if (a_node.is_integer()) {
				if (const auto number = a_node.value<int>(); number && *number >= 0) {
					a_number = *number;
					a_clip.clear();
					return true;
				}
			} else if (const auto name = a_node.value<std::string>()) {
				// as written, but for the whitespace around it and a ".wav"
				auto clip = *name;
				clip.erase(0, clip.find_first_not_of(" \t"));
				clip.erase(clip.find_last_not_of(" \t") + 1);
				if (clip.size() > 4 && Normalize(clip.substr(clip.size() - 4)) == ".wav") {
					clip.resize(clip.size() - 4);
				}
				if (IsFolderName(clip)) {
					a_number = -1;
					a_clip = std::move(clip);
					return true;
				}
			}
			logger::warn("{} must be a voice number (0 or more) or, in quotes, the name of a recording of a Pocket "
						 "model; ignored",
				a_label);
			return false;
		}

		// Several files give one voice type a voice: does the one taken already
		// stay over a_incoming? A voice that is there stays over one nobody
		// installed; then one meant for the voice type over a fill-in (see
		// ModelVoice::fillIn); among equals the later file wins, so kNo.
		enum class Keep
		{
			kNo,
			kInstalled,  // the later one's voice is not installed, the earlier one's is
			kRank        // the later one is a fill-in, the earlier one is not
		};

		Keep KeepExisting(const ModelVoice& a_existing, bool a_existingInstalled, const ModelVoice& a_incoming,
			bool a_incomingInstalled)
		{
			if (!a_incomingInstalled && a_existingInstalled) {
				return Keep::kInstalled;
			}
			if (a_incoming.fillIn && !a_existing.fillIn && a_existingInstalled) {
				return Keep::kRank;
			}
			return Keep::kNo;
		}

		// Reads every models\<folder>\model.toml (see ModelVoice), in folder name
		// order: a voice type two models list goes to the later one, unless that
		// one is a fill-in and the earlier one is not.
		void LoadModelFiles(Settings& a_settings, Installed& a_installed)
		{
			namespace fs = std::filesystem;
			std::map<std::string, fs::path> files;  // by Normalize(folder name)
			std::error_code                 ec;
			// stepped by hand: the ++ of a range-for throws when the folder changes
			// under it, and nothing here may throw
			for (fs::directory_iterator it(fs::path(MODELS_DIR), ec), end; !ec && it != end; it.increment(ec)) {
				std::error_code ignored;
				const auto      file = it->path() / fs::path(MODEL_FILE);
				if (!fs::is_regular_file(file, ignored)) {
					continue;
				}
				try {
					// UTF-8, which is how the rest of the plugin reads a model's name
					const auto utf8 = it->path().filename().u8string();
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
				if (!ParseTomlFile(file, std::format("{} of model", MODEL_FILE), std::format("'{}'", folder), root)) {
					continue;
				}

				if (!ModelInstalled(folder, a_installed)) {
					logger::warn("{} of model '{}': the folder holds no model, ignored", MODEL_FILE, folder);
					continue;
				}

				ModelVoice base;
				base.model = folder;
				base.fillIn = root["fill_in"].value_or(false);
				if (const auto* voice = root["voice"].as_table()) {
					base.speed = SaneSpeed((*voice)["speed"].value_or(base.speed));
					base.volume = SaneVolume((*voice)["volume"].value_or(base.volume));
				}

				std::size_t listed = 0;
				if (const auto* voiceTypes = root["voicetype"].as_table()) {
					for (auto&& [name, node] : *voiceTypes) {
						const auto key = Normalize(name.str());
						const auto label = std::format("{} of model '{}': {}", MODEL_FILE, folder, name.str());
						auto       voice = base;
						if (key.empty() || !ReadVoice(node, voice.voice, voice.clip, label)) {
							continue;
						}
						if (!VoiceInstalled(folder, voice.voice, voice.clip, a_installed)) {
							logger::warn("{}: the model has no {}, ignored", label, VoiceLabel(voice.voice, voice.clip));
							continue;
						}
						if (const auto it = a_settings.modelVoiceTypes.find(key);
							it != a_settings.modelVoiceTypes.end() && it->second.model != folder) {
							// both folders hold a model: this loop reads no other
							if (KeepExisting(it->second, true, voice, true) == Keep::kRank) {
								logger::info("Voice type {} is listed by model '{}' and, as a fill-in, by model '{}': "
											 "'{}' is used",
									name.str(), it->second.model, folder, it->second.model);
								continue;
							}
							if (it->second.fillIn && !voice.fillIn) {
								logger::info("Voice type {} is listed as a fill-in by model '{}' and by model '{}': '{}' "
											 "is used",
									name.str(), it->second.model, folder, folder);
							} else {
								logger::warn("Voice type {} is listed by model '{}' and by model '{}': '{}' is used. A "
											 "[voicetype] line in AudioUtilTTS.toml settles it.",
									name.str(), it->second.model, folder, folder);
							}
						}
						a_settings.modelVoiceTypes[key] = std::move(voice);
						++listed;
					}
				}
				logger::info("Model '{}' lists {} voice types in its {}{}", folder, listed, MODEL_FILE,
					base.fillIn ? ", as fill-in voices" : "");
			}
		}

		// The *.toml files of a folder that mods drop files into, in the order
		// they are read: by Normalize(file name without ".toml"), so that
		// "MyMod.toml" comes before "MyMod - Patch.toml". Each with its name as
		// written and its path. A folder that is not there holds none.
		using TomlFiles = std::map<std::string, std::pair<std::string, std::filesystem::path>>;

		TomlFiles TomlFilesIn(std::string_view a_dir)
		{
			namespace fs = std::filesystem;
			constexpr auto  SUFFIX = ".toml"sv;
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
					if (auto key = Normalize(name); key.ends_with(SUFFIX)) {
						key.resize(key.size() - SUFFIX.size());
						files.emplace(std::move(key), std::pair{ std::move(name), it->path() });
					}
				} catch (const std::exception&) {
					// a name that is not valid text: skipped
				}
			}
			return files;
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

		// A file a mod drops in is read for one table only (and a_alsoOk, a key
		// beside it). Whatever else its author put there is named in the log, so
		// that a [voicetype] table in a preset file, say, is not ignored without a
		// word.
		void WarnOtherTables(const toml::table& a_root, std::string_view a_wanted, std::string_view a_what,
			const std::string& a_name, std::string_view a_hint, std::string_view a_alsoOk = {})
		{
			std::vector<std::string> others;
			for (auto&& [key, node] : a_root) {
				if (key.str() != a_wanted && (a_alsoOk.empty() || key.str() != a_alsoOk)) {
					others.emplace_back(key.str());
				}
			}
			if (!others.empty()) {
				logger::warn("{} {}: only [{}] is read from such a file, ignored: {}. {}", a_what, a_name, a_wanted,
					Listed(others), a_hint);
			}
		}

		// One [preset.<name>] table, from AudioUtilTTS.toml or from a preset file.
		// a_label names it in a warning. nullopt = it is no preset.
		std::optional<Preset> ReadPreset(const toml::node& a_node, const std::string& a_label)
		{
			const auto* table = a_node.as_table();
			if (!table) {
				logger::warn("{} is not a table, ignored", a_label);
				return std::nullopt;
			}
			bool                     known = false;   // it sets something a preset has
			bool                     nested = false;  // it holds a table: the name had a dot in it
			std::vector<std::string> unknown;
			for (auto&& [key, node] : *table) {
				if (key.str() == "model"sv || key.str() == "voice"sv || key.str() == "speed"sv ||
					key.str() == "volume"sv) {
					known = true;
				} else {
					unknown.emplace_back(key.str());
					nested = nested || node.is_table();
				}
			}
			if (nested && !known) {
				logger::warn("{} holds only the table {}: a dot in a preset name makes a nested table. Write the name "
							 "with _ or - instead. Ignored.",
					a_label, Listed(unknown));
				return std::nullopt;
			}
			if (!unknown.empty()) {
				logger::warn("{}: a preset has model, voice, speed and volume; ignored: {}", a_label, Listed(unknown));
			}
			Preset preset;
			preset.model = (*table)["model"].value_or(""s);
			if (const auto* voice = (*table)["voice"].node()) {
				ReadVoice(*voice, preset.voice, preset.clip, a_label + " voice");  // left unset when it is neither
			}
			preset.speed = SaneSpeed((*table)["speed"].value_or(0.0f));
			preset.volume = SaneVolume((*table)["volume"].value_or(-1.0f));
			if (!preset.model.empty() && !IsFolderName(preset.model)) {
				logger::warn("{} model = '{}' is not a plain folder name, using the default model", a_label,
					preset.model);
				preset.model.clear();
			}
			return preset;
		}

		// Can the preset speak in its own voice? One on the default model always
		// counts: whether that model is there is not for the config to say.
		bool PresetInstalled(const Preset& a_preset, Installed& a_installed)
		{
			const auto model = Normalize(a_preset.model);
			return model.empty() || VoiceInstalled(model, a_preset.voice, a_preset.clip, a_installed);
		}

		// Reads every presets\*.toml (see Preset), in file name order, on top of
		// the presets of AudioUtilTTS.toml: a name the user's file defines stays
		// the user's, a name two preset files define goes to the later one, unless
		// only the earlier one's model is installed.
		void LoadPresetFiles(Settings& a_settings, Installed& a_installed)
		{
			for (const auto& [fileKey, file] : TomlFilesIn(PRESET_DIR)) {
				const auto& [name, path] = file;
				toml::table root;
				if (!ParseTomlFile(path, "the preset file", name, root)) {
					continue;
				}
				WarnOtherTables(root, "preset", "Preset file", name,
					"A preset is written as [preset.<name>]; voice types go into a file under voicetypes\\.");
				const auto* presets = root["preset"].as_table();
				if (!presets) {
					logger::warn("Preset file {}: it holds no [preset.<name>] table, nothing was read from it", name);
					continue;
				}

				std::size_t                        added = 0;
				std::set<std::string>              seen;           // names this file defined so far
				std::vector<std::string>           overruled;      // AudioUtilTTS.toml has its own
				std::map<std::string, std::string> missing;        // models nobody installed, as written
				std::vector<std::string>           missingVoices;  // voices that installed models do not have
				for (auto&& [presetName, node] : *presets) {
					const auto id = Normalize(presetName.str());
					const auto label = std::format("{}: [preset.{}]", name, presetName.str());
					if (id.empty()) {
						logger::warn("{} has no name, ignored", label);
						continue;
					}
					auto existing = a_settings.presets.find(id);
					if (existing != a_settings.presets.end() && existing->second.source.empty()) {
						// the user's own: this one is not even looked at
						overruled.emplace_back(presetName.str());
						continue;
					}
					auto preset = ReadPreset(node, label);
					if (!preset) {
						continue;
					}
					const bool installed = PresetInstalled(*preset, a_installed);
					if (!installed && ModelInstalled(Normalize(preset->model), a_installed)) {
						missingVoices.push_back(std::format("{} of '{}' ({})", VoiceLabel(preset->voice, preset->clip),
							preset->model, presetName.str()));
					} else if (!installed) {
						missing.emplace(Normalize(preset->model), preset->model);
					}
					if (!seen.insert(id).second) {
						logger::warn("{} is defined a second time in that file (names ignore case): the later one is used",
							label);
					} else if (existing != a_settings.presets.end()) {
						// a_settings holds only the user's presets and those of earlier files
						if (!installed && PresetInstalled(existing->second, a_installed)) {
							logger::info("Preset {} is defined by the preset files {} and {}: the model of the later one "
										 "is not installed, so {} is kept",
								presetName.str(), existing->second.source, name, existing->second.source);
							continue;
						}
						logger::info("Preset {} is defined by the preset files {} and {}: {} is used", presetName.str(),
							existing->second.source, name, name);
					} else {
						++added;
					}
					preset->source = name;
					a_settings.presets[id] = std::move(*preset);
				}
				std::string notes;
				if (!overruled.empty()) {
					notes += std::format(". Not taken, AudioUtilTTS.toml defines its own: {}", Listed(overruled));
				}
				if (!missing.empty()) {
					notes += std::format(". Models it names that are not installed: {}", Listed(Values(missing)));
				}
				if (!missingVoices.empty()) {
					notes += std::format(". Voices it names that the model does not have: {}", Listed(missingVoices));
				}
				logger::info("Preset file {}: {} new preset{}{}", name, added, added == 1 ? "" : "s", notes);
			}
		}

		// Reads every voicetypes\*.toml (see Settings::patchVoiceTypes), in file
		// name order: a voice type two patches name goes to the later one, unless
		// only the earlier one's voice is installed, or the later one is a fill-in
		// and the earlier one is not.
		void LoadPatchFiles(Settings& a_settings, Installed& a_installed)
		{
			for (const auto& [fileKey, file] : TomlFilesIn(PATCH_DIR)) {
				const auto& [name, path] = file;
				toml::table root;
				if (!ParseTomlFile(path, "the voice type patch", name, root)) {
					continue;
				}
				WarnOtherTables(root, "voicetype", "Voice type patch", name,
					"Voice types are listed under [voicetype]; presets go into a file under presets\\.", "fill_in");
				const bool fillInFile = root["fill_in"].value_or(false);

				std::size_t                        given = 0;
				std::size_t                        unvoiced = 0;  // of those, lines whose voice nobody installed
				std::set<std::string>              seen;          // voice types this file named so far
				std::map<std::string, std::string> missing;       // those voices, as written
				if (const auto* voiceTypes = root["voicetype"].as_table()) {
					for (auto&& [type, node] : *voiceTypes) {
						const auto id = Normalize(type.str());
						const auto label = std::format("Voice type patch {}: {}", name, type.str());
						ModelVoice voice;
						voice.source = name;
						voice.fillIn = fillInFile;
						std::string written;  // the model as the file spells it, for the log
						bool        ok = true;
						if (const auto model = node.value<std::string>()) {
							written = *model;
						} else if (const auto* table = node.as_table()) {
							written = (*table)["model"].value_or(std::string{});
							voice.fillIn = (*table)["fill_in"].value_or(fillInFile);
							voice.speed = SaneSpeed((*table)["speed"].value_or(voice.speed));
							voice.volume = SaneVolume((*table)["volume"].value_or(voice.volume));
							if (const auto* number = (*table)["voice"].node()) {
								ok = ReadVoice(*number, voice.voice, voice.clip, label + " voice");  // warns itself
							}
						}
						if (!ok) {
							continue;
						}
						voice.model = Normalize(written);
						if (id.empty() || !IsFolderName(voice.model)) {
							logger::warn("{} must name a model folder, as \"folder\" or as {{ model = \"folder\", "
										 "voice = 0 }}; ignored",
								label);
							continue;
						}
						const bool installed = VoiceInstalled(voice.model, voice.voice, voice.clip, a_installed);
						const bool again = !seen.insert(id).second;
						if (again) {
							logger::warn("{} is named a second time in that file (names ignore case): the later line "
										 "is used",
								label);
						} else {
							++given;
							unvoiced += installed ? 0 : 1;
						}
						if (!installed) {
							// the model as written, or the voice of it when the model is there
							missing.emplace(voice.model + "|" + VoiceLabel(voice.voice, voice.clip),
								ModelInstalled(voice.model, a_installed) ?
									std::format("{} of '{}'", VoiceLabel(voice.voice, voice.clip), written) :
									written);
						}
						if (const auto it = a_settings.patchVoiceTypes.find(id);
							it != a_settings.patchVoiceTypes.end() && it->second.source != name) {
							// A patch is one of several suggestions for a voice type: the
							// later one counts, but not when its voice is missing and the
							// earlier one's is there, and not as a fill-in over a voice
							// meant for the voice type.
							const bool existingInstalled =
								VoiceInstalled(it->second.model, it->second.voice, it->second.clip, a_installed);
							switch (KeepExisting(it->second, existingInstalled, voice, installed)) {
							case Keep::kInstalled:
								logger::info("Voice type {} is named by the patches {} and {}: the voice of the later "
											 "one is not installed, so {} is kept",
									type.str(), it->second.source, name, it->second.source);
								continue;
							case Keep::kRank:
								logger::info("Voice type {} is named by the patch {} and, as a fill-in, by {}: {} is used",
									type.str(), it->second.source, name, it->second.source);
								continue;
							default:
								logger::info("Voice type {} is named by the patches {} and {}: {} is used", type.str(),
									it->second.source, name, name);
							}
						}
						a_settings.patchVoiceTypes[id] = std::move(voice);
					}
				}
				const auto* types = given == 1 ? "voice type" : "voice types";
				const auto* kind = fillInFile ? ", as fill-in voices" : "";
				if (missing.empty()) {
					logger::info("Voice type patch {}: {} {}{}", name, given, types, kind);
				} else {
					logger::info("Voice type patch {}: {} {}{}; {} of them skipped, their voices are not installed: {}",
						name, given, types, kind, unvoiced, Listed(Values(missing)));
				}
			}
		}

		// replacements\*.toml: the text replacements (see Settings::replacements).
		// Every file is read in file name order; a phrase two files give goes to
		// the later one. The rules end up longest phrase first.
		void LoadReplacementFiles(Settings& a_settings)
		{
			std::map<std::string, TextReplace::Rule> rules;  // by Normalize(phrase)
			for (const auto& [fileKey, file] : TomlFilesIn(REPLACE_DIR)) {
				const auto& [name, path] = file;
				toml::table root;
				if (!ParseTomlFile(path, "the text replacement file", name, root)) {
					continue;
				}
				WarnOtherTables(root, "replace", "Text replacement file", name,
					"Replacements are listed under [replace], as \"what the text says\" = \"what is spoken\".");
				std::size_t given = 0;
				if (const auto* table = root["replace"].as_table()) {
					for (auto&& [phrase, node] : *table) {
						const auto key = Normalize(phrase.str());
						const auto spoken = node.value<std::string>();
						if (key.empty() || !spoken) {
							logger::warn("Text replacement file {}: \"{}\" must be given what is spoken, in quotes "
										 "(\"\" to drop it); ignored",
								name, phrase.str());
							continue;
						}
						if (const auto it = rules.find(key); it != rules.end()) {
							logger::info("Text replacement \"{}\" is given by two files: the one in {} is used", phrase.str(),
								name);
						}
						rules[key] = TextReplace::Rule{ std::string(phrase.str()), *spoken };
						++given;
					}
				}
				++a_settings.replacementFiles;
				if (given > 0) {
					logger::info("Text replacement file {}: {} {}", name, given, given == 1 ? "phrase" : "phrases");
				}
			}
			for (auto& [key, rule] : rules) {
				a_settings.replacements.push_back(std::move(rule));
			}
			// longest first, so that "Nord UBE" is seen before "UBE" eats its tail;
			// the map's order breaks ties, so the result does not change between loads
			std::stable_sort(a_settings.replacements.begin(), a_settings.replacements.end(),
				[](const TextReplace::Rule& a_a, const TextReplace::Rule& a_b) { return a_a.from.size() > a_b.from.size(); });
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
			settings->fillInVoices = (*general)["fill_in_voices"].value_or(settings->fillInVoices);
		}
		if (const auto* model = root["model"].as_table()) {
			settings->model = (*model)["dir"].value_or(settings->model);
			settings->family = (*model)["family"].value_or(settings->family);
			settings->threads = (*model)["threads"].value_or(settings->threads);
			settings->lexicon = (*model)["lexicon"].value_or(settings->lexicon);
			settings->lang = (*model)["lang"].value_or(settings->lang);
			settings->maxLoaded = (*model)["max_loaded"].value_or(settings->maxLoaded);
			if (const auto kept = (*model)["keep_loaded"].value<std::string>()) {
				// "a, b": folder names, comma separated
				settings->keepLoaded.clear();
				std::size_t start = 0;
				while (start <= kept->size()) {
					auto end = kept->find(',', start);
					if (end == std::string::npos) {
						end = kept->size();
					}
					if (const auto name = Normalize(kept->substr(start, end - start)); IsFolderName(name)) {
						settings->keepLoaded.push_back(name);
					}
					start = end + 1;
				}
			}
		}
		if (const auto* voice = root["voice"].as_table()) {
			if (const auto* number = (*voice)["voice"].node()) {
				ReadVoice(*number, settings->voice, settings->voiceClip, "[voice] voice");
			}
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

		if (const auto* player = root["player"].as_table()) {
			settings->player = (*player)["enable"].value_or(settings->player);
			settings->playerPreset = (*player)["preset"].value_or(settings->playerPreset);
			settings->playerMaxWait = (*player)["max_wait"].value_or(settings->playerMaxWait);
			settings->playerGroup = (*player)["group"].value_or(settings->playerGroup);
			settings->playerChannel = (*player)["channel"].value_or(settings->playerChannel);
			settings->playerSlots = (*player)["slots"].value_or(settings->playerSlots);
		}
		settings->playerMaxWait = std::clamp(settings->playerMaxWait, 1.0f, 30.0f);
		settings->playerSlots = std::clamp(settings->playerSlots, 1, 16);

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
		if (const auto key = Normalize(settings->playerChannel);
			key == Normalize(settings->channel) || key == Normalize(settings->speechChannel)) {
			logger::warn("[player] channel must differ from the [playback] and [speech] channels, using 'player'");
			settings->playerChannel = key == "player" ? "player2" : "player";
		}
		settings->outputRate = settings->outputRate <= 0 ? 0 : std::clamp(settings->outputRate, 8000, 96000);

		if (const auto* presets = root["preset"].as_table()) {
			for (auto&& [name, node] : *presets) {
				const auto key = Normalize(name.str());
				const auto label = std::format("[preset.{}]", name.str());
				if (key.empty()) {
					logger::warn("{} has no name, ignored", label);
					continue;
				}
				auto preset = ReadPreset(node, label);
				if (!preset) {
					continue;
				}
				if (settings->presets.contains(key)) {
					logger::warn("{} is defined a second time (names ignore case): the later one is used", label);
				}
				settings->presets[key] = std::move(*preset);
			}
		}
		const auto ownPresets = settings->presets.size();

		Installed installed;
		LoadPresetFiles(*settings, installed);
		LoadModelFiles(*settings, installed);
		LoadPatchFiles(*settings, installed);
		LoadReplacementFiles(*settings);

		std::size_t fillIns = 0;
		for (const auto* map : { &settings->patchVoiceTypes, &settings->modelVoiceTypes }) {
			fillIns += static_cast<std::size_t>(
				std::count_if(map->begin(), map->end(), [](const auto& a_entry) { return a_entry.second.fillIn; }));
		}

		std::string kept;
		for (const auto& name : settings->keepLoaded) {
			kept += (kept.empty() ? "" : ", ") + name;
		}
		logger::info("Config: model '{}' ({}), {}, speed {:.2f}, volume {:.2f}, threads {}, max_loaded {} (kept: {}), "
					 "group '{}', channel '{}', interrupt {}, max_pending {}, slots {}, output_rate {}, {} presets "
					 "({} of them from preset files), speech group '{}', channel '{}', slots {}, lipsync {}{}",
			settings->model, settings->family, VoiceLabel(settings->voice, settings->voiceClip), settings->speed,
			settings->volume, settings->threads, settings->maxLoaded, kept.empty() ? "none" : kept, settings->group,
			settings->channel,
			settings->interrupt, settings->maxPending, settings->slots, settings->outputRate, settings->presets.size(),
			settings->presets.size() - ownPresets, settings->speechGroup, settings->speechChannel,
			settings->speechSlots, settings->speechLipSync, settings->enable ? "" : " [disabled]");
		logger::info("Config: dialogue {}, slots {}, lipsync {} (strength {:.2f}), fallback {}, "
					 "greetings {} (up to {:.1f} s late, in two parts {}), prepare_greetings {}, skip_directions {}, "
					 "cache {} MB, {} voice types mapped, {} given a voice by patches, {} listed by models, {} of "
					 "those fill-in voices ({})",
			settings->dialogue, settings->dialogueSlots, settings->dialogueLip, settings->dialogueLipStrength,
			settings->dialogueFallback, settings->dialogueGreetings, settings->dialogueLateMax,
			settings->dialogueLateSplit, settings->dialoguePrepare,
			settings->dialogueSkipDirections,
			settings->dialogueCacheMb, settings->voiceTypes.size(), settings->patchVoiceTypes.size(),
			settings->modelVoiceTypes.size(), fillIns, settings->fillInVoices ? "on" : "off");
		logger::info("Config: player lines {}, preset '{}', max_wait {:.1f} s, group '{}', channel '{}', slots {}; {} "
					 "text replacements from {} files",
			settings->player, settings->playerPreset, settings->playerMaxWait, settings->playerGroup,
			settings->playerChannel, settings->playerSlots, settings->replacements.size(), settings->replacementFiles);

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
