#include "menu/Schema.h"

// How the menu shows AudioUtilTTS.toml. One entry per key the plugin reads
// (Config.cpp); tests\menu_tests.cpp fails the build when a key of the shipped
// file has no entry here, or an entry names a key that file does not have.
//
// The settings a player changes come first, in groups that are open; what is
// only for an unusual setup goes in the page's last group, kAdvancedGroup,
// which starts closed. Keep tips to what the setting does and what it costs.
//
// Rules: ASCII only (the menu font has no long dashes), no "##" in a label, and
// never state the default in a tip: the menu shows it from the shipped file.

namespace Core
{
	namespace
	{
		SettingSpec S(std::string a_key, std::string a_group, std::string a_label, Control a_control, std::string a_tip)
		{
			SettingSpec spec;
			spec.key = std::move(a_key);
			spec.group = std::move(a_group);
			spec.label = std::move(a_label);
			spec.control = a_control;
			spec.tip = std::move(a_tip);
			return spec;
		}

		// for the page's closed group of settings few people touch
		SettingSpec Advanced(std::string a_key, std::string a_label, Control a_control, std::string a_tip)
		{
			return S(std::move(a_key), std::string{ kAdvancedGroup }, std::move(a_label), a_control, std::move(a_tip));
		}

		SettingSpec Range(SettingSpec a_spec, double a_min, double a_max)
		{
			a_spec.min = a_min;
			a_spec.max = a_max;
			return a_spec;
		}

		// a number field with + and - buttons, never below a_min (a slider needs
		// a top, and a model's voice count is not known here)
		SettingSpec AtLeast(SettingSpec a_spec, double a_min)
		{
			a_spec.min = a_min;
			a_spec.step = 1;
			return a_spec;
		}

		// a number field with a step, between two bounds
		SettingSpec Field(SettingSpec a_spec, double a_min, double a_max, double a_step, std::string a_unit = {})
		{
			a_spec.min = a_min;
			a_spec.max = a_max;
			a_spec.step = a_step;
			a_spec.unit = std::move(a_unit);
			return a_spec;
		}

		SettingSpec Unit(SettingSpec a_spec, std::string a_unit)
		{
			a_spec.unit = std::move(a_unit);
			return a_spec;
		}

		SettingSpec Applying(SettingSpec a_spec, Applies a_applies)
		{
			a_spec.applies = a_applies;
			return a_spec;
		}

		// a preset key the menu offers in every [preset.<name>] table
		SettingSpec EveryPreset(SettingSpec a_spec, std::string a_inherit, std::optional<Scalar> a_fallback = std::nullopt)
		{
			a_spec.everyTable = true;
			a_spec.inherit = std::move(a_inherit);
			a_spec.fallback = std::move(a_fallback);
			return a_spec;
		}

		Schema Make()
		{
			std::vector<PageSpec> pages{
				{ "Dialogue", "Dialogue lines that have no voice file, spoken in the voice of the speaker's voice type.", { "dialogue" } },
				{ "Voice types", "Which preset speaks for a voice type. A voice archive named after a voice type needs no line here.", { "voicetype" } },
				{ "Voice", "The default voice: narration, and lines that name no voice of their own.", { "general", "model", "voice" } },
				{ "Presets", "Named voices mods ask for. A setting left out comes from the default voice.", { "preset.*" } },
				{ "Narration & speech", "Lines mods send: narration has no speaker, character speech is said by an actor.", { "playback", "speech" } },
			};

			std::vector<SettingSpec> settings{
				// ------------------------------------------------------------- Dialogue
				S("dialogue.enable", "Unvoiced dialogue", "On", Control::Flag,
					"Speak dialogue lines that have no voice file. Turning it off works at once; turning it on again needs a game start when it was off at the last one."),
				S("dialogue.fallback", "Unvoiced dialogue", "Generic voice for the rest", Control::Flag,
					"Speak the voice types that have no voice of their own too, in a generic female or male voice. Needs the default voice model."),
				S("dialogue.skip_directions", "Unvoiced dialogue", "Skip stage directions", Control::Flag,
					"Leave out text in (parentheses), [brackets] or *asterisks*."),
				S("dialogue.greetings", "New lines said right away", "Speak them late", Control::Flag,
					"A new greeting or opening line cannot be generated in time. On: it is spoken as soon as it is ready, a moment after its subtitle. Off: it stays silent the first time."),
				Unit(Range(S("dialogue.greetings_max_late", "New lines said right away", "At most", Control::Float,
							  "Later than this, the line stays silent that first time."),
						 0.5, 10.0),
					"s"),
				S("dialogue.greetings_split", "New lines said right away", "Start with the first sentence", Control::Flag,
					"A long line is spoken in two parts, so that it starts sooner."),
				S("dialogue.prepare_greetings", "New lines said right away", "Make greetings ahead (heavy)", Control::Flag,
					"Generates the greetings of NPCs near you before they say them. Costs CPU and memory, most of all in towns."),
				S("dialogue.lipsync", "Mouth", "Move the mouth", Control::Flag,
					"Shapes follow the loudness of the line: it reads as talking, not as the actual sounds."),
				Range(S("dialogue.lip_strength", "Mouth", "Mouth opening", Control::Float, "How far the mouth opens."), 0.0, 1.5),
				Applying(Range(Advanced("dialogue.slots", "Placeholder files", Control::Int,
									"Placeholder voice files for dialogue lines. A line that finds none free stays silent."),
							 64, 256),
					Applies::Restart),
				Applying(Field(Advanced("dialogue.cache_max_mb", "Cache size limit", Control::Int,
									"Generated lines are kept on disk; the ones used longest ago go first. 0 = no limit."),
							 0, 16384, 256, "MB"),
					Applies::Restart),

				// ---------------------------------------------------------- Voice types
				S("voicetype.*", "Lines in your file", "*", Control::Text,
					"The preset that speaks for the voice type *. Empty = never spoken for."),

				// ---------------------------------------------------------------- Voice
				Applying(S("general.enable", "Speech", "Speech on", Control::Flag,
							 "Master switch. Off loads no voice model and speaks nothing."),
					Applies::Reload),
				AtLeast(S("voice.voice", "Default voice", "Voice number", Control::Int,
							"Voice number inside the default model. kokoro-en-v0_19: 0 af, 1 af_bella, 2 af_nicole (whispered), 3 af_sarah, 4 af_sky, 5 am_adam, 6 am_michael, 7 bf_emma, 8 bf_isabella, 9 bm_george, 10 bm_lewis. Try one on the Status page."),
					0),
				Range(S("voice.speed", "Default voice", "Speed", Control::Float, "1.0 is the model's natural pace."), 0.5, 2.0),
				Range(S("voice.volume", "Default voice", "Volume", Control::Float, "Before AudioUtil's group volume."), 0.0, 1.0),
				Applying(S("model.dir", "Default voice", "Model folder", Control::Text,
							 "A folder under Data\\SKSE\\Plugins\\AudioUtilTTS\\models. Optional: without it only voices with a model of their own speak."),
					Applies::Reload),
				Applying(Range(S("model.threads", "Performance", "Threads per line", Control::Int,
								   "More threads get a line ready sooner, and take those CPU cores from the game while it is made."),
							 1, 16),
					Applies::Reload),
				Range(S("model.max_loaded", "Performance", "Models in memory", Control::Int,
						  "Voice models kept loaded at once, the default one included. About 0.6 GB each; loading one again delays its next line by about a second."),
					1, 32),
				Applying(Advanced("model.family", "Model family", Control::Text,
							 "auto reads the folder (voices.bin means Kokoro, otherwise VITS), or kokoro, or vits."),
					Applies::Reload),
				Applying(Advanced("model.lexicon", "Lexicon files", Control::Text,
							 "Only for multi-language Kokoro builds: file names inside the model folder, comma separated."),
					Applies::Reload),
				Applying(Advanced("model.lang", "Language", Control::Text, "Only for models that need it."), Applies::Reload),

				// -------------------------------------------------------------- Presets
				EveryPreset(S("preset.*.model", "Preset *", "Model folder", Control::Text,
								"Empty = the default model."),
					{}, Scalar{ std::string{} }),
				EveryPreset(AtLeast(S("preset.*.voice", "Preset *", "Voice number", Control::Int,
										"Voice number inside the preset's model."),
								0),
					"voice.voice"),
				EveryPreset(Range(S("preset.*.speed", "Preset *", "Speed", Control::Float, "1.0 is the model's natural pace."), 0.5, 2.0),
					"voice.speed"),
				EveryPreset(Range(S("preset.*.volume", "Preset *", "Volume", Control::Float, "Before AudioUtil's group volume."), 0.0, 1.0),
					"voice.volume"),

				// --------------------------------------------------- Narration & speech
				Range(S("playback.max_pending", "Lines in a row", "Lines that may wait", Control::Int,
						  "0: a line that comes while another is being made or spoken is dropped. More: that many wait their turn. Narration and character speech each count on their own."),
					0, 16),
				S("playback.interrupt", "Lines in a row", "A new line cuts in", Control::Flag,
					"A new line cuts into the one being spoken instead of waiting for it to end."),
				Field(S("playback.max_chars", "Lines in a row", "Longest line", Control::Int,
						  "Longer text is cut at the last sentence that fits. Dialogue lines too."),
					16, 4000, 50, "characters"),
				S("speech.lipsync", "Character speech", "Move the speaker's mouth", Control::Flag,
					"AudioUtil lipsync, driven by loudness. Needs AudioUtil 0.9.26 or newer."),
				Advanced("playback.group", "Narration volume group", Control::Text, "The AudioUtil volume group narration plays in."),
				Advanced("playback.channel", "Narration channel", Control::Text, "The AudioUtil channel narration plays on. Must differ from the speech channel."),
				Advanced("speech.group", "Speech volume group", Control::Text, "The AudioUtil volume group character speech plays in."),
				Advanced("speech.channel", "Speech channel", Control::Text, "The AudioUtil channel character speech plays on. Must differ from the narration channel."),
				Applying(Range(Advanced("playback.slots", "Narration placeholder files", Control::Int,
									"Placeholder wav files narration is written into."),
							 1, 16),
					Applies::Restart),
				Applying(Range(Advanced("speech.slots", "Speech placeholder files", Control::Int,
									"Placeholder wav files character lines are written into."),
							 1, 16),
					Applies::Restart),
				Field(Advanced("playback.output_rate", "Output sample rate", Control::Int,
						  "0 = the model's own. Set 44100 if speech plays at the wrong pitch or speed. Every line, dialogue too."),
					0, 48000, 100, "Hz"),
			};
			return Schema{ std::move(pages), std::move(settings) };
		}
	}

	const Schema& TtsSchema()
	{
		static const Schema schema = Make();
		return schema;
	}
}
