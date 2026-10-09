#include "Speaker.h"

#include "Config.h"
#include "TtsEngine.h"

#include <API/AudioUtilAPI.h>

#include <Windows.h>

#include <array>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <optional>
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
		constexpr auto SPEAK_EVENT = "AudioUtilTTS_Speak"sv;    // received: narrate strArg
		constexpr auto SAY_EVENT = "AudioUtilTTS_Say"sv;        // received: the sending actor says strArg

		// AudioUtil_GetVersion() from which lipsync follows a rewritten file (0.9.26).
		// Older ones would replay the mouth movement of the first line a slot held.
		constexpr std::uint32_t LIPSYNC_FOLLOWS_REWRITES = 900026;

		enum Lane : std::size_t
		{
			kNarration,
			kSpeech,
			kLaneCount
		};

		constexpr std::array<std::string_view, kLaneCount> SLOT_PREFIX{ "_tts_slot"sv, "_tts_speech"sv };
		constexpr std::array<std::string_view, kLaneCount> LANE_NAME{ "narration"sv, "speech"sv };

		struct Line
		{
			std::string     text;
			std::string     model;  // normalized folder name, "" = the default model
			int             voice = -1;
			float           volume = -1.0f;
			float           speed = -1.0f;
			Lane            lane = kNarration;
			RE::ActorHandle speaker;  // speech lane only
			bool            lipSync = false;
			std::uint64_t   epoch = 0;
		};

		struct LaneState
		{
			std::vector<bool>       slotUsable;
			std::unordered_set<int> slotsCreated;  // made this session: not indexed until a restart
			int                     nextSlot = 0;
			Clock::time_point       quietAt{};  // when the line now playing on this lane ends
		};

		std::mutex                        g_lock;  // guards the block below
		std::condition_variable           g_wake;
		std::deque<Line>                  g_waiting;
		bool                              g_reloadWanted = false;
		bool                              g_started = false;
		std::string                       g_problem;  // why the default model is not loaded
		std::array<LaneState, kLaneCount> g_lanes;
		std::unordered_set<std::string>   g_unknownPresets;  // already warned about

		std::atomic<std::uint64_t> g_epoch{ 1 };  // bumped by Cancel: lines from an older epoch are dropped
		std::atomic<bool>          g_enabled{ true };
		std::atomic<bool>          g_working{ false };
		std::atomic<bool>          g_warnedNotReady{ false };
		std::atomic<bool>          g_warnedOldLipSync{ false };

		std::array<std::atomic<int>, kLaneCount> g_usableSlots{};

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

		// ---------------------------------------------------------------- AudioUtil

		using PlayFileFn = decltype(&AudioUtil_PlayFile);

		std::atomic<PlayFileFn>    g_playFile{ nullptr };
		std::atomic<PlayFileFn>    g_playFileLipSync{ nullptr };  // same signature
		std::atomic<std::uint32_t> g_audioUtilVersion{ 0 };

		PlayFileFn ResolvePlayFile()
		{
			if (const auto known = g_playFile.load()) {
				return known;
			}
			const auto module = GetModuleHandleA("AudioUtil.dll");
			if (!module) {
				return nullptr;
			}
			if (const auto version =
					reinterpret_cast<decltype(&AudioUtil_GetVersion)>(GetProcAddress(module, "AudioUtil_GetVersion"))) {
				g_audioUtilVersion.store(version());
			}
			g_playFileLipSync.store(
				reinterpret_cast<PlayFileFn>(GetProcAddress(module, "AudioUtil_PlayFileWithLipSync")));
			const auto found = reinterpret_cast<PlayFileFn>(GetProcAddress(module, "AudioUtil_PlayFile"));
			g_playFile.store(found);
			return found;
		}

		// -------------------------------------------------------------------- slots

		fs::path DataRoot() { return fs::current_path() / "Data"; }

		std::string SlotName(Lane a_lane, int a_i)
		{
			return std::format("{}{:02d}.wav", SLOT_PREFIX[a_lane], a_i);
		}
		std::string SlotDataRel(Lane a_lane, int a_i)
		{
			return std::string(SLOT_DIR_REL) + "\\" + SlotName(a_lane, a_i);
		}
		fs::path SlotDisk(Lane a_lane, int a_i) { return DataRoot() / SLOT_DIR_REL / SlotName(a_lane, a_i); }

		// A slot is playable only if the game indexed it at launch, so one created
		// now starts working after the next restart.
		void PrepareSlots(Lane a_lane, int a_count)
		{
			std::error_code ec;
			fs::create_directories(DataRoot() / SLOT_DIR_REL, ec);

			std::scoped_lock  lock{ g_lock };
			auto&             lane = g_lanes[a_lane];
			std::vector<bool> usable(static_cast<std::size_t>(a_count), false);
			int               ready = 0;
			int               created = 0;
			for (int i = 0; i < a_count; ++i) {
				const auto disk = SlotDisk(a_lane, i);
				if (fs::exists(disk, ec) && fs::file_size(disk, ec) >= 44) {
					if (!lane.slotsCreated.contains(i)) {
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
					lane.slotsCreated.insert(i);
					++created;
				} else {
					logger::warn("Slot {} could not be created: {}", SlotDataRel(a_lane, i), error);
				}
			}
			lane.slotUsable = std::move(usable);
			lane.nextSlot = 0;
			g_usableSlots[a_lane].store(ready);
			logger::info("Slots for {}: {} of {} playable now{}", LANE_NAME[a_lane], ready, a_count,
				created ? std::format(", {} created (playable after the next game restart)", created) : ""s);
		}

		int TakeSlot(Lane a_lane)
		{
			std::scoped_lock lock{ g_lock };
			auto&            lane = g_lanes[a_lane];
			const int        count = static_cast<int>(lane.slotUsable.size());
			for (int step = 0; step < count; ++step) {
				const int i = (lane.nextSlot + step) % count;
				if (lane.slotUsable[static_cast<std::size_t>(i)]) {
					lane.nextSlot = (i + 1) % count;
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
				const bool speech = line.lane == kSpeech;

				// a character line needs its character: keep them alive across the call
				RE::NiPointer<RE::Actor> speaker;
				if (speech) {
					speaker = line.speaker.get();
					if (!speaker || !speaker->Is3DLoaded()) {
						logger::info("Speaker is gone or not loaded, a {}-char line was dropped", line.text.size());
						return;
					}
				}

				auto call = play;
				bool lipSync = false;
				if (speech && line.lipSync) {
					const auto withLipSync = g_playFileLipSync.load();
					if (withLipSync && g_audioUtilVersion.load() >= LIPSYNC_FOLLOWS_REWRITES) {
						call = withLipSync;
						lipSync = true;
					} else if (!g_warnedOldLipSync.exchange(true)) {
						logger::warn("Character lines play without lipsync: that needs AudioUtil 0.9.26 or newer");
					}
				}

				const auto& group = speech ? settings->speechGroup : settings->group;
				const auto& channel = speech ? settings->speechChannel : settings->channel;
				const auto  handle = call(slotRel.c_str(), speaker.get(), line.volume, group.c_str(), channel.c_str());
				if (handle <= 0) {
					logger::warn("AudioUtil did not play {} (see AudioUtil.log)", slotRel);
					return;
				}
				logger::info("Spoke {} chars as {:.1f} s of speech, generated in {} ms ({}{}, model {}, voice {}, {}, "
							 "handle {})",
					line.text.size(), a_seconds, a_synthMs, LANE_NAME[line.lane],
					speech ? std::format(" by {:08X}{}", speaker->GetFormID(), lipSync ? " with lipsync" : "") : ""s,
					line.model.empty() ? "default"s : line.model, line.voice, slotRel, handle);
				if (auto* source = SKSE::GetModCallbackEventSource()) {
					SKSE::ModCallbackEvent event{ RE::BSFixedString(SPOKEN_EVENT), RE::BSFixedString(line.text),
						static_cast<float>(handle), speaker.get() };
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
			const Lane lane = a_line.lane;

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

			const int slot = TakeSlot(lane);
			if (slot < 0) {
				logger::warn("No playable {} slot (they work from the launch after they are created), line dropped",
					LANE_NAME[lane]);
				return;
			}
			if (!TtsEngine::WriteWav(SlotDisk(lane, slot), audio, error)) {
				logger::warn("Could not write {}: {}", SlotDataRel(lane, slot), error);
				return;
			}

			const auto length = std::chrono::milliseconds(static_cast<long long>(audio.Seconds() * 1000.0f));
			{
				std::unique_lock lock{ g_lock };
				if (!settings->interrupt) {
					// let the line now playing on this lane finish; a Cancel wakes this early
					const auto until = g_lanes[lane].quietAt;
					g_wake.wait_until(lock, until, [&] { return !current(); });
				}
				if (!current()) {
					return;
				}
				g_lanes[lane].quietAt = Clock::now() + length;
			}
			PostPlay(SlotDataRel(lane, slot), std::move(a_line), audio.Seconds(), synthMs);
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

		std::string NotReadyReason(Lane a_lane)
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
			if (g_usableSlots[a_lane].load() <= 0) {
				return std::format("no playable {} slot yet (restart the game once)", LANE_NAME[a_lane]);
			}
			return {};
		}

		// Takes a line whose voice fields may still be "not set" and fills them in.
		bool Queue(std::string_view a_text, Line a_line)
		{
			const auto settings = Config::Get();
			if (!g_enabled.load() || !settings->enable) {
				return false;
			}
			if (const auto reason = NotReadyReason(a_line.lane); !reason.empty()) {
				if (!g_warnedNotReady.exchange(true)) {
					logger::warn("A {} line was ignored: {}", LANE_NAME[a_line.lane], reason);
				}
				return false;
			}

			a_line.text = Clean(a_text, settings->maxChars);
			if (a_line.text.empty()) {
				return false;
			}
			if (a_line.voice < 0) {
				a_line.voice = settings->voice;
			}
			a_line.volume = a_line.volume >= 0.0f ? std::min(a_line.volume, 1.0f) : settings->volume;
			if (a_line.speed <= 0.0f) {
				a_line.speed = settings->speed;
			}
			{
				std::scoped_lock lock{ g_lock };
				a_line.epoch = g_epoch.load();
				// newest wins inside a lane: speech must not fall far behind the scene
				std::size_t same = 0;
				for (const auto& waiting : g_waiting) {
					same += waiting.lane == a_line.lane;
				}
				for (auto it = g_waiting.begin();
					 same >= static_cast<std::size_t>(settings->maxPending) && it != g_waiting.end();) {
					if (it->lane == a_line.lane) {
						it = g_waiting.erase(it);
						--same;
					} else {
						++it;
					}
				}
				g_waiting.push_back(std::move(a_line));
			}
			g_wake.notify_all();
			return true;
		}

		// The voice fields of a preset, ready to drop into a Line. nullopt = no such preset.
		std::optional<Line> FromPreset(const Config::Settings& a_settings, const std::string& a_key)
		{
			const auto it = a_settings.presets.find(a_key);
			if (it == a_settings.presets.end()) {
				return std::nullopt;
			}
			const auto& preset = it->second;
			Line        line;
			line.model = Config::Normalize(preset.model);
			if (line.model == Config::Normalize(a_settings.model)) {
				line.model.clear();
			}
			// a voice number means something only inside its own model, so a preset
			// on another model that names no voice gets that model's first one
			line.voice = preset.voice >= 0 ? preset.voice : (line.model.empty() ? a_settings.voice : 0);
			line.volume = preset.volume;
			line.speed = preset.speed;
			return line;
		}

		void WarnUnknownPreset(const std::string& a_key)
		{
			bool first = false;
			{
				std::scoped_lock lock{ g_lock };
				first = g_unknownPresets.insert(a_key).second;
			}
			if (first) {
				logger::warn("Preset '{}' is not defined in AudioUtilTTS.toml, using the default voice", a_key);
			}
		}

		// "Name" -> ""  and  "Name:preset" -> "preset"; nullopt when a_event is another event
		std::optional<std::string_view> PresetOf(std::string_view a_event, std::string_view a_name)
		{
			if (a_event.size() < a_name.size() || _strnicmp(a_event.data(), a_name.data(), a_name.size()) != 0) {
				return std::nullopt;
			}
			const auto rest = a_event.substr(a_name.size());
			if (rest.empty()) {
				return std::string_view{};
			}
			if (rest.front() == ':') {
				return rest.substr(1);
			}
			return std::nullopt;
		}

		// The entry points with no compile-time dependency: any script can send
		// AudioUtilTTS_Speak or AudioUtilTTS_Say. Every mod event in the game passes
		// through here, on the sender's thread.
		class SpeakEventSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event,
				RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
			{
				if (a_event) {
					const std::string_view name = a_event->eventName.c_str();
					if (const auto preset = PresetOf(name, SPEAK_EVENT)) {
						Speaker::SpeakAs(a_event->strArg.c_str(), *preset);
					} else if (const auto sayPreset = PresetOf(name, SAY_EVENT)) {
						if (auto* actor = a_event->sender ? a_event->sender->As<RE::Actor>() : nullptr) {
							Speaker::SpeakActor(actor, a_event->strArg.c_str(), *sayPreset, true);
						} else {
							logger::warn("{} ignored: it must be sent by the actor who speaks", SAY_EVENT);
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
		PrepareSlots(kNarration, settings->slots);
		PrepareSlots(kSpeech, settings->speechSlots);
		if (!ResolvePlayFile()) {
			logger::error("AudioUtil.dll is not loaded: nothing can be spoken. Install AudioUtil 0.9.9 or newer.");
		} else {
			logger::info("AudioUtil found (version {})", g_audioUtilVersion.load());
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
		const auto settings = Config::Get();
		PrepareSlots(kNarration, settings->slots);
		PrepareSlots(kSpeech, settings->speechSlots);
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
			for (auto& lane : g_lanes) {
				lane.quietAt = {};
			}
		}
		g_wake.notify_all();
	}

	bool Speak(std::string_view a_text, int a_voice, float a_volume, float a_speed)
	{
		Line line;
		line.voice = a_voice;
		line.volume = a_volume;
		line.speed = a_speed;
		return Queue(a_text, std::move(line));
	}

	bool SpeakAs(std::string_view a_text, std::string_view a_preset)
	{
		const auto key = Config::Normalize(a_preset);
		Line       line;
		if (!key.empty()) {
			if (auto found = FromPreset(*Config::Get(), key)) {
				line = std::move(*found);
			} else {
				WarnUnknownPreset(key);
			}
		}
		return Queue(a_text, std::move(line));
	}

	bool HasPreset(std::string_view a_preset)
	{
		return Config::Get()->presets.contains(Config::Normalize(a_preset));
	}

	bool SpeakActor(RE::Actor* a_actor, std::string_view a_text, std::string_view a_preset, bool a_lipSync)
	{
		if (!a_actor) {
			return false;
		}
		const auto settings = Config::Get();
		auto       key = Config::Normalize(a_preset);
		Line       line;
		if (key.empty()) {
			// no preset named: go by the speaker's sex when the config defines those
			const auto* base = a_actor->GetActorBase();
			key = base && base->IsFemale() ? "female" : "male";
			if (auto found = FromPreset(*settings, key)) {
				line = std::move(*found);
			}
		} else if (auto found = FromPreset(*settings, key)) {
			line = std::move(*found);
		} else {
			WarnUnknownPreset(key);
		}
		line.lane = kSpeech;
		line.speaker = a_actor->GetHandle();
		line.lipSync = a_lipSync && settings->speechLipSync;
		return Queue(a_text, std::move(line));
	}

	bool IsReady()
	{
		return g_enabled.load() && Config::Get()->enable && NotReadyReason(kNarration).empty();
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
		if (const auto reason = NotReadyReason(kNarration); !reason.empty()) {
			return "not ready: " + reason;
		}
		const bool lipSync = settings->speechLipSync && g_playFileLipSync.load() &&
							 g_audioUtilVersion.load() >= LIPSYNC_FOLLOWS_REWRITES;
		return std::format("{} | model {} ({} voices, {} Hz) | voice {} | {} presets | {} models loaded | "
						   "narration slots {} of {} | speech slots {} of {}, lipsync {} | sherpa-onnx {}",
			IsEnabled() ? "ready" : "switched off", settings->model, g_defaultVoices.load(), g_defaultRate.load(),
			settings->voice, settings->presets.size(), g_modelsLoaded.load(), g_usableSlots[kNarration].load(),
			settings->slots, g_usableSlots[kSpeech].load(), settings->speechSlots,
			lipSync ? "on" : (settings->speechLipSync ? "off (AudioUtil older than 0.9.26)" : "off"),
			TtsEngine::RuntimeVersion());
	}

	std::string Channel()
	{
		return Config::Get()->channel;
	}

	std::string SpeechChannel()
	{
		return Config::Get()->speechChannel;
	}
}
