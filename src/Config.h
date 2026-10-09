#pragma once

// Data\SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml, read at kDataLoaded and on
// ReloadConfig. Every key is optional; the defaults below are the shipped file.
namespace Config
{
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
	};

	// false = the file did not parse and the previous settings were kept
	bool Load();

	std::shared_ptr<const Settings> Get();
}
