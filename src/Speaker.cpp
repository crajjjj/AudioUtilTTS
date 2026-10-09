#include "Speaker.h"

#include "Config.h"
#include "TtsEngine.h"

#include <API/AudioUtilAPI.h>

#include <Windows.h>

#include <condition_variable>
#include <cstring>
#include <deque>
#include <thread>

namespace Speaker
{
	namespace
	{
		namespace fs = std::filesystem;
		using Clock = std::chrono::steady_clock;

		constexpr auto MODELS_DIR = "Data\\SKSE\\Plugins\\AudioUtilTTS\\models\\"sv;
		constexpr auto SLOT_DIR_REL = "Sound\\AudioUtilTTS"sv;
		constexpr auto SPOKEN_EVENT = "AudioUtilTTS_Spoken"sv;  // sent when a line starts playing
		constexpr auto SPEAK_EVENT = "AudioUtilTTS_Speak"sv;    // received: speak strArg

		struct Line
		{
			std::string   text;
			std::string   model;  // normalized folder name, "" = the default model
			int           voice = 0;
			float         volume = 1.0f;
			float         speed = 1.0f;
			std::uint64_t epoch = 0;
		};

		std::mutex                      g_lock;  // guards the block below
		std::condition_variable         g_wake;
		std::deque<Line>                g_waiting;
		bool                            g_reloadWanted = false;
		bool                            g_started = false;
		Clock::time_point               g_quietAt{};  // when the line now playing ends
		std::string                     g_problem;    // why the default model is not loaded
		std::vector<bool>               g_slotUsable;
		std::unordered_set<int>         g_slotsCreated;  // made this session: not indexed until a restart
		int                             g_nextSlot = 0;
		std::unordered_set<std::string> g_unknownPresets;  // already warned about

		std::atomic<std::uint64_t> g_epoch{ 1 };  // bumped by Cancel: lines from an older epoch are dropped
		std::atomic<bool>          g_enabled{ true };
		std::atomic<bool>          g_working{ false };
		std::atomic<int>           g_usableSlots{ 0 };
		std::atomic<bool>          g_warnedNotReady{ false };

		// the default model, as seen from other threads
		std::atomic<bool> g_defaultLoaded{ false };
		std::atomic<int>  g_defaultVoices{ 0 };
		std::atomic<int>  g_defaultRate{ 0 };
		std::atomic<int>  g_modelsLoaded{ 0 };

		// worker thread only
		struct LoadedModel
		{
			std::unique_ptr<TtsEngine::Model> model;
			std::uint64_t                     lastUsed = 0;
		};
		std::unordered_map<std::string, LoadedModel> g_models;     // keyed by normalized folder name
		std::unordered_set<std::string>              g_badModels;  // failed to load: not retried until a reload
		std::uint64_t                                g_useClock = 0;

		using PlayFileFn = decltype(&AudioUtil_PlayFile);
		std::atomic<PlayFileFn> g_playFile{ nullptr };

		// ---------------------------------------------------------------- AudioUtil

		PlayFileFn ResolvePlayFile()
		{
			if (const auto known = g_playFile.load()) {
				return known;
			}
			const auto module = GetModuleHandleA("AudioUtil.dll");
			if (!module) {
				return nullptr;
			}
			const auto found = reinterpret_cast<PlayFileFn>(GetProcAddress(module, "AudioUtil_PlayFile"));
			g_playFile.store(found);
			return found;
		}

		// -------------------------------------------------------------------- slots

		fs::path    DataRoot() { return fs::current_path() / "Data"; }
		std::string SlotName(int a_i) { return std::format("_tts_slot{:02d}.wav", a_i); }
		std::string SlotDataRel(int a_i) { return std::string(SLOT_DIR_REL) + "\\" + SlotName(a_i); }
		fs::path    SlotDisk(int a_i) { return DataRoot() / SLOT_DIR_REL / SlotName(a_i); }

		// A slot is playable only if the game indexed it at launch, so one created
		// now starts working after the next restart.
		void PrepareSlots(int a_count)
		{
			std::error_code ec;
			fs::create_directories(DataRoot() / SLOT_DIR_REL, ec);

			std::scoped_lock  lock{ g_lock };
			std::vector<bool> usable(static_cast<std::size_t>(a_count), false);
			int               ready = 0;
			int               created = 0;
			for (int i = 0; i < a_count; ++i) {
				const auto disk = SlotDisk(i);
				if (fs::exists(disk, ec) && fs::file_size(disk, ec) >= 44) {
					if (!g_slotsCreated.contains(i)) {
						usable[static_cast<std::size_t>(i)] = true;
						++ready;
					}
					continue;
				}
				TtsEngine::Audio silence;
				silence.samples.assign(1000, 0.0f);
				silence.sampleRate = 44100;
				std::string error;
				if (TtsEngine::WriteWav(disk, silence, error)) {
					g_slotsCreated.insert(i);
					++created;
				} else {
					logger::warn("Slot {} could not be created: {}", SlotDataRel(i), error);
				}
			}
			g_slotUsable = std::move(usable);
			g_nextSlot = 0;
			g_usableSlots.store(ready);
			logger::info("Slots: {} of {} playable now{}", ready, a_count,
				created ? std::format(", {} created (playable after the next game restart)", created) : ""s);
		}

		int TakeSlot()
		{
			std::scoped_lock lock{ g_lock };
			const int        count = static_cast<int>(g_slotUsable.size());
			for (int step = 0; step < count; ++step) {
				const int i = (g_nextSlot + step) % count;
				if (g_slotUsable[static_cast<std::size_t>(i)]) {
					g_nextSlot = (i + 1) % count;
					return i;
				}
			}
			return -1;
		}

		// --------------------------------------------------------------------- text

		// One line of plain text: control characters and runs of whitespace become
		// single spaces, and anything past a_maxChars is cut at a sentence end.
		std::string Clean(std::string_view a_text, int a_maxChars)
		{
			std::string out;
			out.reserve(a_text.size());
			bool space = false;
			for (const char c : a_text) {
				const auto u = static_cast<unsigned char>(c);
				if (u <= 0x20 || u == 0x7F) {
					space = !out.empty();
					continue;
				}
				if (space) {
					out += ' ';
					space = false;
				}
				out += c;
			}

			const auto cap = static_cast<std::size_t>(a_maxChars);
			if (out.size() <= cap) {
				return out;
			}
			auto cut = out.find_last_of(".!?", cap - 1);
			if (cut != std::string::npos && cut >= cap / 2) {
				return out.substr(0, cut + 1);
			}
			cut = out.rfind(' ', cap - 1);
			if (cut != std::string::npos && cut >= cap / 2) {
				return out.substr(0, cut);
			}
			cut = cap;
			while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
				--cut;  // never split a UTF-8 sequence
			}
			return out.substr(0, cut);
		}

		// ------------------------------------------------------------------- models

		std::unique_ptr<TtsEngine::Model> LoadModel(const std::string& a_folder, const Config::Settings& a_settings,
			bool a_isDefault, std::string& a_error)
		{
			TtsEngine::ModelSpec spec;
			spec.dir = std::string(MODELS_DIR) + a_folder;
			spec.threads = a_settings.threads;
			if (a_isDefault) {
				// family / lexicon / lang describe the [model] dir only; a model named
				// by a preset is read by what its folder contains
				spec.family = a_settings.family;
				spec.lexicon = a_settings.lexicon;
				spec.lang = a_settings.lang;
			}
			const auto start = Clock::now();
			auto       model = TtsEngine::Model::Load(spec, a_error);
			if (model) {
				const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
				logger::info("Model '{}' loaded in {} ms: {} voices, {} Hz", a_folder, ms, model->VoiceCount(),
					model->SampleRate());
			}
			return model;
		}

		// Drops every loaded model and loads the default one again.
		void LoadDefaultModel()
		{
			const auto settings = Config::Get();
			g_defaultLoaded.store(false);
			g_defaultVoices.store(0);
			g_defaultRate.store(0);
			g_models.clear();
			g_badModels.clear();
			g_modelsLoaded.store(0);

			std::string problem;
			if (!settings->enable) {
				problem = "disabled in AudioUtilTTS.toml";
				logger::info("Speech is disabled in the config, no model loaded");
			} else if (auto model = LoadModel(settings->model, *settings, true, problem)) {
				if (settings->voice >= model->VoiceCount()) {
					logger::warn("[voice] voice = {} but the model has {} voices, using the last one", settings->voice,
						model->VoiceCount());
				}
				g_defaultVoices.store(model->VoiceCount());
				g_defaultRate.store(model->SampleRate());
				g_models[Config::Normalize(settings->model)] = LoadedModel{ std::move(model), ++g_useClock };
				g_modelsLoaded.store(1);
				g_defaultLoaded.store(true);
			} else {
				logger::error("Model '{}' not loaded: {}", settings->model, problem);
			}
			std::scoped_lock lock{ g_lock };
			g_problem = std::move(problem);
		}

		// The model a line asked for, loaded on first use. A model that cannot be
		// loaded falls back to the default one (a_fellBack).
		const TtsEngine::Model* ModelFor(const std::string& a_wanted, const Config::Settings& a_settings,
			bool& a_fellBack)
		{
			a_fellBack = false;
			const auto defaultKey = Config::Normalize(a_settings.model);
			const auto key = a_wanted.empty() ? defaultKey : a_wanted;
			const auto use = [](const std::string& a_key) -> const TtsEngine::Model* {
				const auto it = g_models.find(a_key);
				if (it == g_models.end()) {
					return nullptr;
				}
				it->second.lastUsed = ++g_useClock;
				return it->second.model.get();
			};

			if (const auto* loaded = use(key)) {
				return loaded;
			}
			if (key != defaultKey && !g_badModels.contains(key)) {
				if (a_settings.maxLoaded < 2) {
					logger::warn("Model '{}' not loaded: [model] max_loaded = 1 keeps only the default model", key);
					g_badModels.insert(key);
				} else {
					// make room first; the default model always stays
					while (static_cast<int>(g_models.size()) >= a_settings.maxLoaded) {
						auto oldest = g_models.end();
						for (auto it = g_models.begin(); it != g_models.end(); ++it) {
							if (it->first != defaultKey &&
								(oldest == g_models.end() || it->second.lastUsed < oldest->second.lastUsed)) {
								oldest = it;
							}
						}
						if (oldest == g_models.end()) {
							break;
						}
						logger::info("Model '{}' unloaded to make room for '{}'", oldest->first, key);
						g_models.erase(oldest);
					}
					std::string error;
					if (auto model = LoadModel(key, a_settings, false, error)) {
						const auto* raw = model.get();
						g_models[key] = LoadedModel{ std::move(model), ++g_useClock };
						g_modelsLoaded.store(static_cast<int>(g_models.size()));
						return raw;
					}
					logger::error("Model '{}' not loaded: {}. Lines asking for it use the default model.", key, error);
					g_badModels.insert(key);
					g_modelsLoaded.store(static_cast<int>(g_models.size()));
				}
			}
			a_fellBack = key != defaultKey;
			return use(defaultKey);
		}

		// ------------------------------------------------------------------- worker

		void PostPlay(std::string a_slotRel, Line a_line, float a_seconds, long long a_synthMs)
		{
			auto* task = SKSE::GetTaskInterface();
			if (!task) {
				return;
			}
			task->AddTask([slotRel = std::move(a_slotRel), line = std::move(a_line), a_seconds, a_synthMs]() {
				if (line.epoch != g_epoch.load()) {
					return;
				}
				const auto play = ResolvePlayFile();
				if (!play) {
					return;
				}
				const auto settings = Config::Get();
				const auto handle =
					play(slotRel.c_str(), nullptr, line.volume, settings->group.c_str(), settings->channel.c_str());
				if (handle <= 0) {
					logger::warn("AudioUtil did not play {} (see AudioUtil.log)", slotRel);
					return;
				}
				logger::info("Spoke {} chars as {:.1f} s of speech, generated in {} ms (model {}, voice {}, {}, handle {})",
					line.text.size(), a_seconds, a_synthMs, line.model.empty() ? "default"s : line.model, line.voice,
					slotRel, handle);
				if (auto* source = SKSE::GetModCallbackEventSource()) {
					SKSE::ModCallbackEvent event{ RE::BSFixedString(SPOKEN_EVENT), RE::BSFixedString(line.text),
						static_cast<float>(handle), nullptr };
					source->SendEvent(&event);
				}
			});
		}

		void Process(Line a_line)
		{
			const auto current = [&] { return a_line.epoch == g_epoch.load(); };
			if (!current()) {
				return;
			}
			const auto settings = Config::Get();

			bool        fellBack = false;
			const auto* model = ModelFor(a_line.model, *settings, fellBack);
			if (!model) {
				return;  // no default model either; LoadDefaultModel already said why
			}
			if (fellBack) {
				// the voice number belonged to the model that is not there
				a_line.model.clear();
				a_line.voice = settings->voice;
			}
			if (!current()) {
				return;  // loading a model can take a second: the line may be stale by now
			}

			TtsEngine::Audio audio;
			std::string      error;
			const auto       start = Clock::now();
			const auto result = model->Synthesize(a_line.text, a_line.voice, a_line.speed, current, audio, error);
			if (result == TtsEngine::Result::Cancelled) {
				return;
			}
			if (result != TtsEngine::Result::Ok) {
				logger::warn("Could not speak a {}-char line: {}", a_line.text.size(), error);
				return;
			}
			const auto synthMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
			TtsEngine::Resample(audio, settings->outputRate);

			const int slot = TakeSlot();
			if (slot < 0) {
				logger::warn("No playable slot (they work from the launch after they are created), line dropped");
				return;
			}
			if (!TtsEngine::WriteWav(SlotDisk(slot), audio, error)) {
				logger::warn("Could not write {}: {}", SlotDataRel(slot), error);
				return;
			}

			const auto length = std::chrono::milliseconds(static_cast<long long>(audio.Seconds() * 1000.0f));
			{
				std::unique_lock lock{ g_lock };
				if (!settings->interrupt) {
					// let the line now playing finish; a Cancel wakes this early
					const auto until = g_quietAt;
					g_wake.wait_until(lock, until, [&] { return !current(); });
				}
				if (!current()) {
					return;
				}
				g_quietAt = Clock::now() + length;
			}
			PostPlay(SlotDataRel(slot), std::move(a_line), audio.Seconds(), synthMs);
		}

		void Worker()
		{
			LoadDefaultModel();
			for (;;) {
				Line line;
				{
					std::unique_lock lock{ g_lock };
					g_wake.wait(lock, [] { return g_reloadWanted || !g_waiting.empty(); });
					if (g_reloadWanted) {
						g_reloadWanted = false;
						lock.unlock();
						LoadDefaultModel();
						continue;
					}
					line = std::move(g_waiting.front());
					g_waiting.pop_front();
					g_working.store(true);
				}
				Process(std::move(line));
				g_working.store(false);
			}
		}

		// -------------------------------------------------------------------- intake

		std::string NotReadyReason()
		{
			if (!ResolvePlayFile()) {
				return "AudioUtil.dll is not loaded (AudioUtil 0.9.9 or newer is required)";
			}
			{
				std::scoped_lock lock{ g_lock };
				if (!g_started) {
					return "still starting";
				}
				if (!g_problem.empty()) {
					return g_problem;
				}
			}
			if (!g_defaultLoaded.load()) {
				return "the model is still loading";
			}
			if (g_usableSlots.load() <= 0) {
				return "no playable slot yet (restart the game once)";
			}
			return {};
		}

		// a_model is a normalized folder name, "" = the default model
		bool Queue(std::string_view a_text, std::string a_model, int a_voice, float a_volume, float a_speed)
		{
			const auto settings = Config::Get();
			if (!g_enabled.load() || !settings->enable) {
				return false;
			}
			if (const auto reason = NotReadyReason(); !reason.empty()) {
				if (!g_warnedNotReady.exchange(true)) {
					logger::warn("Speak ignored: {}", reason);
				}
				return false;
			}

			Line line;
			line.text = Clean(a_text, settings->maxChars);
			if (line.text.empty()) {
				return false;
			}
			line.model = std::move(a_model);
			line.voice = a_voice >= 0 ? a_voice : settings->voice;
			line.volume = a_volume >= 0.0f ? std::min(a_volume, 1.0f) : settings->volume;
			line.speed = a_speed > 0.0f ? a_speed : settings->speed;
			{
				std::scoped_lock lock{ g_lock };
				line.epoch = g_epoch.load();
				// newest wins: speech must not fall far behind what is on screen
				while (g_waiting.size() >= static_cast<std::size_t>(settings->maxPending)) {
					g_waiting.pop_front();
				}
				g_waiting.push_back(std::move(line));
			}
			g_wake.notify_all();
			return true;
		}

		// The entry point with no compile-time dependency: any script can
		// SendModEvent("AudioUtilTTS_Speak", text), or "AudioUtilTTS_Speak:<preset>".
		// Every mod event in the game passes through here, on the sender's thread.
		class SpeakEventSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event,
				RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
			{
				if (a_event) {
					const std::string_view name = a_event->eventName.c_str();
					if (name.size() >= SPEAK_EVENT.size() &&
						_strnicmp(name.data(), SPEAK_EVENT.data(), SPEAK_EVENT.size()) == 0) {
						const auto rest = name.substr(SPEAK_EVENT.size());
						if (rest.empty()) {
							Speaker::Speak(a_event->strArg.c_str(), -1, -1.0f, -1.0f);
						} else if (rest.front() == ':') {
							Speaker::SpeakAs(a_event->strArg.c_str(), rest.substr(1));
						}
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		SpeakEventSink g_speakSink;
	}

	void Start()
	{
		const auto settings = Config::Get();
		PrepareSlots(settings->slots);
		if (!ResolvePlayFile()) {
			logger::error("AudioUtil.dll is not loaded: nothing can be spoken. Install AudioUtil 0.9.9 or newer.");
		}

		std::scoped_lock lock{ g_lock };
		if (!g_started) {
			g_started = true;
			if (auto* source = SKSE::GetModCallbackEventSource()) {
				source->AddEventSink(&g_speakSink);
			}
			// never joined: it lives for the whole process, like AudioUtil's tickers
			std::thread(Worker).detach();
		}
	}

	bool Reload()
	{
		const bool parsed = Config::Load();
		Cancel();
		PrepareSlots(Config::Get()->slots);
		{
			std::scoped_lock lock{ g_lock };
			g_reloadWanted = true;
			g_unknownPresets.clear();
		}
		g_warnedNotReady.store(false);
		g_wake.notify_all();
		return parsed;
	}

	void Cancel()
	{
		{
			std::scoped_lock lock{ g_lock };
			g_epoch.fetch_add(1);
			g_waiting.clear();
			g_quietAt = {};
		}
		g_wake.notify_all();
	}

	bool Speak(std::string_view a_text, int a_voice, float a_volume, float a_speed)
	{
		return Queue(a_text, {}, a_voice, a_volume, a_speed);
	}

	bool SpeakAs(std::string_view a_text, std::string_view a_preset)
	{
		const auto key = Config::Normalize(a_preset);
		if (key.empty()) {
			return Queue(a_text, {}, -1, -1.0f, -1.0f);
		}
		const auto settings = Config::Get();
		const auto it = settings->presets.find(key);
		if (it == settings->presets.end()) {
			bool first = false;
			{
				std::scoped_lock lock{ g_lock };
				first = g_unknownPresets.insert(key).second;
			}
			if (first) {
				logger::warn("Preset '{}' is not defined in AudioUtilTTS.toml, using the default voice", key);
			}
			return Queue(a_text, {}, -1, -1.0f, -1.0f);
		}

		const auto& preset = it->second;
		auto        model = Config::Normalize(preset.model);
		if (model == Config::Normalize(settings->model)) {
			model.clear();
		}
		// a voice number means something only inside its own model, so a preset
		// on another model that names no voice gets that model's first one
		const int voice = preset.voice >= 0 ? preset.voice : (model.empty() ? settings->voice : 0);
		return Queue(a_text, std::move(model), voice, preset.volume, preset.speed);
	}

	bool HasPreset(std::string_view a_preset)
	{
		return Config::Get()->presets.contains(Config::Normalize(a_preset));
	}

	bool IsReady()
	{
		return g_enabled.load() && Config::Get()->enable && NotReadyReason().empty();
	}

	bool IsBusy()
	{
		if (g_working.load()) {
			return true;
		}
		std::scoped_lock lock{ g_lock };
		return !g_waiting.empty();
	}

	void SetEnabled(bool a_enabled)
	{
		g_enabled.store(a_enabled);
		if (!a_enabled) {
			Cancel();
		}
	}

	bool IsEnabled()
	{
		return g_enabled.load() && Config::Get()->enable;
	}

	int VoiceCount()
	{
		return g_defaultVoices.load();
	}

	std::string Status()
	{
		const auto settings = Config::Get();
		if (const auto reason = NotReadyReason(); !reason.empty()) {
			return "not ready: " + reason;
		}
		return std::format("{} | model {} ({} voices, {} Hz) | voice {} | {} presets | {} models loaded | "
						   "slots {} of {} | sherpa-onnx {}",
			IsEnabled() ? "ready" : "switched off", settings->model, g_defaultVoices.load(), g_defaultRate.load(),
			settings->voice, settings->presets.size(), g_modelsLoaded.load(), g_usableSlots.load(), settings->slots,
			TtsEngine::RuntimeVersion());
	}

	std::string Channel()
	{
		return Config::Get()->channel;
	}
}
