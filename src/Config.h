#pragma once

// Data\SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml, read at kDataLoaded and on
// ReloadConfig. Every key is optional; the defaults below are the shipped file.
namespace Config
{
	// A named voice a caller asks for by name: [preset.<name>]. Anything left out
	// is inherited from the defaults.
	struct Preset
	{
		std::string model;           // "" = the [model] dir
		int         voice = -1;      // < 0 = not set
		float       speed = 0.0f;    // <= 0 = not set
		float       volume = -1.0f;  // < 0 = not set
	};

	struct Settings
	{
		// [general]
		bool enable = true;

		// [model]
		std::string model = "kokoro-en-v0_19";  // folder under ...\AudioUtilTTS\models
		std::string family = "auto";
		int         threads = 2;
		std::string lexicon;
		std::string lang;
		int         maxLoaded = 3;

		// [voice]
		int   voice = 7;
		float speed = 1.0f;
		float volume = 1.0f;

		// [playback]
		std::string group = "narration";
		std::string channel = "narration";
		bool        interrupt = false;
		int         maxPending = 1;
		int         maxChars = 400;
		int         slots = 4;
		int         outputRate = 0;

		// [speech]: lines spoken by a character (SpeakActor / AudioUtilTTS_Say)
		std::string speechGroup = "speech";
		std::string speechChannel = "speech";
		int         speechSlots = 4;
		bool        speechLipSync = true;

		// [preset.<name>], keyed by Normalize(name)
		std::unordered_map<std::string, Preset> presets;
	};

	// false = the file did not parse and the previous settings were kept
	bool Load();

	std::shared_ptr<const Settings> Get();

	// Lowercase with surrounding whitespace removed: how preset names and model
	// folder names are compared.
	std::string Normalize(std::string_view a_text);
}
