#include "menu/Schema.h"

// How the menu shows AudioUtilTTS.toml. One entry per key the plugin reads
// (Config.cpp); tests\menu_tests.cpp fails the build when a key of the shipped
// file has no entry here, or an entry names a key that file does not have.
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
				{ "Voice", "The default voice, and the model it comes from.", { "general", "model", "voice" } },
				{ "Presets", "Named voices a mod asks for by name. A setting a preset leaves out comes from the default voice. Presets a mod brings in presets\\*.toml are not shown here.", { "preset.*" } },
				{ "Dialogue", "Game dialogue that has no voice file, spoken in the voice of the speaker's voice type.", { "dialogue" } },
				{ "Narration", "Lines with no speaker that mods send. The first group governs character speech too.", { "playback" } },
				{ "Character speech", "Lines a mod has an actor say: positioned at them, with their mouth moving.", { "speech" } },
				{ "Voice types", "Which preset speaks for a voice type. A voice archive named after a voice type needs no line here; a line here wins over it.", { "voicetype" } },
			};

			std::vector<SettingSpec> settings{
				// ---------------------------------------------------------------- Voice
				Applying(S("general.enable", "Speech", "Speech on", Control::Flag,
							 "Master switch. Off loads no voice model and speaks nothing."),
					Applies::Reload),
				AtLeast(S("voice.voice", "Default voice", "Voice number", Control::Int,
							"Speaker number inside the default model. kokoro-en-v0_19: 0 af, 1 af_bella, 2 af_nicole (whispered), 3 af_sarah, 4 af_sky, 5 am_adam, 6 am_michael, 7 bf_emma, 8 bf_isabella, 9 bm_george, 10 bm_lewis. The Status page lets you hear one."),
					0),
				Range(S("voice.speed", "Default voice", "Speed", Control::Float, "1.0 is the model's natural pace."), 0.5, 2.0),
				Range(S("voice.volume", "Default voice", "Volume", Control::Float, "Before AudioUtil's group volume is applied."), 0.0, 1.0),
				Applying(S("model.dir", "Default model", "Model folder", Control::Text,
							 "A folder under Data\\SKSE\\Plugins\\AudioUtilTTS\\models. Optional: without it, narration and the generic voices are silent, and every voice with a model of its own still speaks."),
					Applies::Reload),
				Applying(S("model.family", "Default model", "Model family", Control::Text,
							 "auto reads the folder: voices.bin present means Kokoro, otherwise VITS (Piper voices). Or kokoro, or vits."),
					Applies::Reload),
				Applying(S("model.lexicon", "Default model", "Lexicon files", Control::Text,
							 "Only for models that need them (multi-language Kokoro builds): file names inside the model folder, comma separated."),
					Applies::Reload),
				Applying(S("model.lang", "Default model", "Language", Control::Text, "Only for models that need it."), Applies::Reload),
				Applying(Range(S("model.threads", "Performance", "Threads per line", Control::Int,
								   "CPU threads used while a line is being generated. More threads get a line ready sooner, and take those cores from the game for that moment. For a 4.5 second line on an i7-12700KF: 1 thread 3.2 s, 2 threads 1.9 s, 4 threads 1.2 s."),
							 1, 16),
					Applies::Reload),
				Range(S("model.max_loaded", "Performance", "Models in memory", Control::Int,
						  "How many voice models may be loaded at once, the default one included. A Kokoro model takes about 0.6 GB while loaded. Past the limit the one used longest ago is unloaded, and loading it again delays its next line by about a second."),
					1, 32),

				// -------------------------------------------------------------- Presets
				EveryPreset(S("preset.*.model", "Preset *", "Model folder", Control::Text,
								"Empty = the default model. Another model is loaded the first time a line asks for it."),
					{}, Scalar{ std::string{} }),
				EveryPreset(AtLeast(S("preset.*.voice", "Preset *", "Voice number", Control::Int,
										"Speaker number inside the preset's model. Left out: the default voice number on the default model, voice 0 on another model."),
								0),
					"voice.voice"),
				EveryPreset(Range(S("preset.*.speed", "Preset *", "Speed", Control::Float, "1.0 is the model's natural pace."), 0.5, 2.0),
					"voice.speed"),
				EveryPreset(Range(S("preset.*.volume", "Preset *", "Volume", Control::Float, "Before AudioUtil's group volume is applied."), 0.0, 1.0),
					"voice.volume"),

				// ------------------------------------------------------------- Dialogue
				S("dialogue.enable", "Unvoiced dialogue", "Speak unvoiced dialogue", Control::Flag,
					"Off stops it at once, and from the next game start the game is left unpatched. Turning it on again works at once when it was on at game start, else after the next one."),
				S("dialogue.fallback", "Unvoiced dialogue", "Generic voice for the rest", Control::Flag,
					"On: a voice type that has no voice of its own is spoken with the preset female or male by its sex, which needs the default model. Off: only the voice types that have a voice are spoken."),
				S("dialogue.skip_directions", "Unvoiced dialogue", "Skip stage directions", Control::Flag,
					"Leave out text in (parentheses), [brackets] or *asterisks*."),
				S("dialogue.greetings", "Lines said right away", "Speak new lines late", Control::Flag,
					"A new greeting, or another line said the moment its topic starts, is spoken as soon as it is generated, a moment after its subtitle, instead of staying unvoiced the first time."),
				[] {
					auto spec = Range(S("dialogue.greetings_max_late", "Lines said right away", "Latest", Control::Float,
										  "The longest a line may be late and still be spoken. Later than this it stays unvoiced that once."),
						0.5, 10.0);
					spec.unit = "s";
					return spec;
				}(),
				S("dialogue.greetings_split", "Lines said right away", "Start a long line with its first sentence", Control::Flag,
					"A long line that is late is generated in two parts, so that it is heard sooner. Only that first time; the cache gets the whole line."),
				S("dialogue.prepare_greetings", "Lines said right away", "Make greetings ahead", Control::Flag,
					"Generates the greetings of NPCs near you before they say them, so that a new one is on time. Heavy, most of all in towns: up to 3 lines for every NPC you come near, and their voices are loaded for it. Leave it off when the game is short of CPU or memory."),
				S("dialogue.lipsync", "Mouth", "Move the mouth", Control::Flag,
					"Mouth shapes guessed from the loudness of the line: it reads as talking, it does not form the actual sounds."),
				Range(S("dialogue.lip_strength", "Mouth", "Mouth opening", Control::Float, "How far the mouth opens."), 0.0, 1.5),
				Applying(Range(S("dialogue.slots", "Files", "Placeholder files", Control::Int,
								   "Placeholder voice files for dialogue lines. A line that finds none free stays unvoiced. Files added by raising this work after the next game start."),
							 64, 256),
					Applies::Restart),
				[] {
					auto spec = Applying(S("dialogue.cache_max_mb", "Files", "Cache size limit", Control::Int,
											 "Generated lines are kept on disk, and the ones used longest ago go first. 0 = no limit. The cache is trimmed at game start and on a reload."),
						Applies::Restart);
					spec.unit = "MB";
					spec.min = 0;
					spec.max = 16384;
					spec.step = 256;
					return spec;
				}(),

				// ------------------------------------------------------------ Narration
				Range(S("playback.max_pending", "Lines in a row", "Lines that may wait", Control::Int,
						  "0: a line that arrives while another is being generated or spoken is dropped, so a mod that sends lines faster than they can be spoken is not read out back to back. 1 or more: that many wait their turn. Narration and character speech are counted each on its own."),
					0, 16),
				S("playback.interrupt", "Lines in a row", "A new line cuts in", Control::Flag,
					"A new line cuts into the one being spoken as soon as it is ready, instead of waiting for it to end."),
				[] {
					auto spec = S("playback.max_chars", "Lines in a row", "Longest line", Control::Int,
						"Longer text is cut at the last sentence that fits. Dialogue lines too.");
					spec.unit = "characters";
					spec.min = 16;
					spec.max = 4000;
					spec.step = 50;
					return spec;
				}(),
				S("playback.group", "AudioUtil", "Volume group", Control::Text, "The AudioUtil volume group narration plays in."),
				S("playback.channel", "AudioUtil", "Channel", Control::Text, "The AudioUtil channel narration plays on. It must differ from the character speech channel."),
				Applying(Range(S("playback.slots", "Files", "Placeholder files", Control::Int,
								   "Placeholder wav files narration is written into. Files added by raising this work after the next game start."),
							 1, 16),
					Applies::Restart),
				[] {
					auto spec = S("playback.output_rate", "Files", "Output sample rate", Control::Int,
						"0 = the model's own rate (24000 for Kokoro). Set 44100 if speech plays at the wrong pitch or speed. Applies to every line, dialogue too.");
					spec.unit = "Hz";
					spec.min = 0;
					spec.max = 48000;
					spec.step = 100;
					return spec;
				}(),

				// ----------------------------------------------------- Character speech
				S("speech.lipsync", "Character speech", "Move the mouth", Control::Flag,
					"AudioUtil lipsync, driven by loudness. Needs AudioUtil 0.9.26 or newer."),
				S("speech.group", "AudioUtil", "Volume group", Control::Text, "The AudioUtil volume group character speech plays in."),
				S("speech.channel", "AudioUtil", "Channel", Control::Text, "The AudioUtil channel character speech plays on. It must differ from the narration channel."),
				Applying(Range(S("speech.slots", "Files", "Placeholder files", Control::Int,
								   "Placeholder wav files character lines are written into. Files added by raising this work after the next game start."),
							 1, 16),
					Applies::Restart),

				// ---------------------------------------------------------- Voice types
				S("voicetype.*", "Lines in your file", "*", Control::Text,
					"The preset that speaks for the voice type *. Empty = this voice type is never spoken for."),
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
