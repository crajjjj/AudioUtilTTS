#pragma once

#include "TextReplace.h"

// Data\SKSE\Plugins\AudioUtilTTS\AudioUtilTTS.toml, read at kDataLoaded and on
// ReloadConfig. Every key is optional; the defaults below are the shipped file.
namespace Config
{
	// Where the model folders are, relative to the game folder.
	inline constexpr std::string_view MODELS_DIR = "Data\\SKSE\\Plugins\\AudioUtilTTS\\models\\";
	// What a model says about itself, inside its own folder (see ModelVoice).
	inline constexpr std::string_view MODEL_FILE = "model.toml";
	// Where the voice type patches are (see Settings::patchVoiceTypes).
	inline constexpr std::string_view PATCH_DIR = "Data\\SKSE\\Plugins\\AudioUtilTTS\\voicetypes\\";
	// Where the preset files are (see Preset::source).
	inline constexpr std::string_view PRESET_DIR = "Data\\SKSE\\Plugins\\AudioUtilTTS\\presets\\";
	// Where the text replacement files are (see Settings::replacements).
	inline constexpr std::string_view REPLACE_DIR = "Data\\SKSE\\Plugins\\AudioUtilTTS\\replacements\\";

	// A named voice a caller asks for by name: [preset.<name>]. Anything left out
	// is inherited from the defaults.
	//
	// AudioUtilTTS.toml is the user's, so a mod cannot put its presets there.
	// It brings them as a file of its own instead, presets\<any name>.toml,
	// holding the same [preset.<name>] tables. Every such file is read, in file
	// name order: a name two files define goes to the later one (unless only
	// the earlier one's model is installed), and a name AudioUtilTTS.toml
	// defines stays the user's, whole.
	// Such a preset is for lines that ask for it by name. It does not speak for
	// a voice type by being named like one (Speaker's VoiceForType): that is
	// what a voice type patch is for.
	//
	// "voice" is a number, or in quotes the name of one of a Pocket model's
	// recordings (voices\<name>.wav, see TtsEngine::Clip): that one is `clip`
	// here, and it counts over `voice`.
	struct Preset
	{
		std::string model;           // "" = the [model] dir
		int         voice = -1;      // < 0 = not set
		std::string clip;            // a recording's name, "" = not set
		float       speed = 0.0f;    // <= 0 = not set
		float       volume = -1.0f;  // < 0 = not set
		std::string source;          // the preset file it comes from, "" = AudioUtilTTS.toml
	};

	// The voice a model offers for one voice type, from models\<folder>\model.toml:
	//   fill_in = false   # optional, see below
	//   [voicetype]
	//   MaleNord = 0      # voice type editor ID = voice number inside this model
	//   MaleOrc = "gruff" # or the name of one of a Pocket model's recordings
	//   [voice]           # optional, for every voice type the file lists
	//   speed = 1.0
	//   volume = 1.0
	// The file travels with the model, so a model can be installed, updated and
	// removed on its own, with no line in AudioUtilTTS.toml.
	//
	// A fill-in voice (fill_in = true) is offered for voice types that nothing
	// else gives a voice: it ranks below every other way a voice type finds its
	// voice, a model folder named like the voice type included, so that a set of
	// stand-in voices for the voice types a user has no trained voice for never
	// takes a voice type away from a trained voice they add later. Among the
	// files that name one voice type, one meant for it beats a fill-in whatever
	// the file order. Settings::fillInVoices switches them off as a whole.
	struct ModelVoice
	{
		std::string model;           // Normalize(folder name)
		int         voice = 0;
		std::string clip;            // a recording's name; counts over voice when set
		bool        fillIn = false;
		float       speed = 0.0f;    // <= 0 = not set
		float       volume = -1.0f;  // < 0 = not set
		std::string source;          // the patch file that says so, "" for a model.toml
	};

	struct Settings
	{
		// [general]
		bool enable = true;
		bool fillInVoices = true;  // fill_in_voices: use the fill-in voices (see ModelVoice)

		// [model]
		std::string model = "kokoro-en-v0_19";  // folder under ...\AudioUtilTTS\models
		std::string family = "auto";
		int         threads = 2;
		std::string lexicon;
		std::string lang;
		int         maxLoaded = 4;
		// keep_loaded: Normalize(folder name)s never unloaded to make room (the
		// default model never is anyway): for a model many voices share, such as
		// a Pocket model. max_loaded counts the other models only.
		std::vector<std::string> keepLoaded{ "pocket-tts" };

		// [voice]
		int         voice = 7;
		std::string voiceClip;  // voice given as a recording's name (Pocket); counts over voice when set
		float       speed = 1.0f;
		float volume = 1.0f;

		// [playback]
		std::string group = "narration";
		std::string channel = "narration";
		bool        interrupt = false;
		int         maxPending = 0;  // 0 = a line that finds its lane busy is dropped
		int         maxChars = 400;
		int         slots = 4;
		int         outputRate = 0;

		// [speech]: lines spoken by a character (SpeakActor / AudioUtilTTS_Say)
		std::string speechGroup = "speech";
		std::string speechChannel = "speech";
		int         speechSlots = 4;
		bool        speechLipSync = true;

		// [dialogue]: game dialogue that has no recorded voice file
		bool  dialogue = true;
		int   dialogueSlots = 128;
		bool  dialogueLip = true;
		float dialogueLipStrength = 0.8f;
		bool  dialogueFallback = false;
		bool  dialoguePrepare = false;  // prepare_greetings
		bool  dialogueGreetings = true;  // greetings: a new greeting is spoken late, once it is ready
		float dialogueLateMax = 4.0f;    // greetings_max_late: seconds (0.5 to 10); later than this it is not spoken
		bool  dialogueLateSplit = true;  // greetings_split: a long line spoken late starts with its first sentence
		bool  dialogueSkipDirections = true;
		int   dialogueCacheMb = 1024;

		// [player]: the player's own lines in a conversation, for a voice mod
		// that holds the conversation open for them (Dragonborn ReVoiced, see
		// DBReV.h). The voice is a preset, never the player's voice type.
		bool        player = true;
		std::string playerPreset;          // "" = the preset "female" / "male" by the player's sex
		float       playerMaxWait = 5.0f;  // max_wait: seconds the conversation may wait for a line (1 to 30)
		std::string playerGroup = "speech";
		std::string playerChannel = "player";
		int         playerSlots = 4;

		// [skyrimnet]: SkyrimNet's lines, through the XTTS endpoint it talks to
		// (XttsServer.h). SkyrimNet plays them itself; the plugin only makes them.
		bool        skyrimnet = false;                  // enable: answer, while SkyrimNet is installed
		int         skyrimnetPort = 8020;               // port: SkyrimNet's default XTTS address
		bool        skyrimnetCloning = false;           // voice_cloning: take the voice samples it sends
		std::string skyrimnetCloneModel = "pocket-tts";  // clone_model: the Pocket model those are spoken with

		// [preset.<name>], keyed by Normalize(name)
		std::unordered_map<std::string, Preset> presets;

		// [voicetype]: Normalize(voice type editor ID) -> Normalize(preset name).
		// An empty preset name means that voice type is never spoken for.
		std::unordered_map<std::string, std::string> voiceTypes;

		// The voice type patches, voicetypes\*.toml: Normalize(voice type editor
		// ID) -> the voice of an installed model that speaks for it. A patch is a
		// file of its own, so the voice types of a mod can be given voices by
		// dropping one in, and several patches add up (in file name order; a
		// voice type two of them name goes to the later one):
		//   fill_in = false                                   # optional, for the whole file
		//   [voicetype]
		//   HjoromirVoice = "MaleYoungEager"                  # the model folder
		//   RumarinVoice = { model = "my-voices", voice = 3 }  # speed, volume, fill_in too
		//   OrcVoice = { model = "pocket-tts", voice = "gruff" }  # a recording by name
		// Asked after the two maps above and before the model.toml files; a
		// fill-in entry (see ModelVoice) after everything else.
		std::unordered_map<std::string, ModelVoice> patchVoiceTypes;

		// The model.toml files: Normalize(voice type editor ID) -> the voice a
		// model offers for it. Asked after the maps above, so the main config
		// always has the last word.
		std::unordered_map<std::string, ModelVoice> modelVoiceTypes;

		// Words and phrases replaced in every line before it is spoken, from
		// the files replacements\*.toml (all of them, in file name order):
		//   [replace]
		//   "Nord UBE" = "Nord"   # what the text says = what is spoken
		//   UBE = ""              # or dropped
		// A phrase two files replace goes to the later one. Matched whole and
		// ignoring ASCII case (TextReplace.h); longer phrases are applied first,
		// so "Nord UBE" wins over "UBE" where both match.
		std::vector<TextReplace::Rule> replacements;
		std::size_t                    replacementFiles = 0;  // files the rules came from
	};

	// false = the file did not parse and the previous settings were kept
	bool Load();

	std::shared_ptr<const Settings> Get();

	// Lowercase with surrounding whitespace removed: how preset names, model
	// folder names and voice type IDs are compared.
	std::string Normalize(std::string_view a_text);

	// A model name becomes part of a path: it must be one plain folder name.
	bool IsFolderName(const std::string& a_name);
}
