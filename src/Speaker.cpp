#include "Speaker.h"

#include "Config.h"
#include "TtsEngine.h"

#include <API/AudioUtilAPI.h>

#include <Windows.h>

#include <condition_variable>
#include <deque>
#include <thread>
#include <unordered_set>

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
			int           voice = 0;
			float         volume = 1.0f;
			float         speed = 1.0f;
			std::uint64_t epoch = 0;
		};

		std::mutex              g_lock;  // guards the block below
		std::condition_variable g_wake;
		std::deque<Line>        g_waiting;
		bool                    g_reloadWanted = false;
		bool                    g_started = false;
		Clock::time_point       g_quietAt{};  // when the line now playing ends
		std::string             g_problem;    // why the model is not loaded
		std::vector<bool>       g_slotUsable;
		std::unordered_set<int> g_slotsCreated;  // made this session: not indexed until a restart
		int                     g_nextSlot = 0;

		std::atomic<std::uint64_t> g_epoch{ 1 };  // bumped by Cancel: lines from an older epoch are dropped
		std::atomic<bool>          g_enabled{ true };
		std::atomic<bool>          g_working{ false };
		std::atomic<int>           g_usableSlots{ 0 };
		std::atomic<bool>          g_warnedNotReady{ false };

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

		// ------------------------------------------------------------------- worker

		void LoadModel()
		{
			const auto  settings = Config::Get();
			std::string problem;
			if (!settings->enable) {
				TtsEngine::Unload();
				problem = "disabled in AudioUtilTTS.toml";
				logger::info("Speech is disabled in the config, model not loaded");
			} else {
				TtsEngine::ModelSpec spec;
				spec.dir = std::string(MODELS_DIR) + settings->model;
				spec.family = settings->family;
				spec.lexicon = settings->lexicon;
				spec.lang = settings->lang;
				spec.threads = settings->threads;

				const auto start = Clock::now();
				if (TtsEngine::Load(spec, problem)) {
					const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
					logger::info("Model '{}' loaded in {} ms: {} voices, {} Hz (sherpa-onnx {})", settings->model, ms,
						TtsEngine::VoiceCount(), TtsEngine::SampleRate(), TtsEngine::RuntimeVersion());
					if (settings->voice >= TtsEngine::VoiceCount()) {
						logger::warn("[voice] voice = {} but the model has {} voices, using the last one",
							settings->voice, TtsEngine::VoiceCount());
					}
				} else {
					logger::error("Model '{}' not loaded: {}", settings->model, problem);
				}
			}
			std::scoped_lock lock{ g_lock };
			g_problem = std::move(problem);
		}

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
				logger::info("Spoke {} chars as {:.1f} s of speech, generated in {} ms (voice {}, {}, handle {})",
					line.text.size(), a_seconds, a_synthMs, line.voice, slotRel, handle);
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

			TtsEngine::Audio audio;
			std::string      error;
			const auto       start = Clock::now();
			const auto result = TtsEngine::Synthesize(a_line.text, a_line.voice, a_line.speed, current, audio, error);
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
			LoadModel();
			for (;;) {
				Line line;
				{
					std::unique_lock lock{ g_lock };
					g_wake.wait(lock, [] { return g_reloadWanted || !g_waiting.empty(); });
					if (g_reloadWanted) {
						g_reloadWanted = false;
						lock.unlock();
						LoadModel();
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

		// The entry point with no compile-time dependency: any script can
		// SendModEvent("AudioUtilTTS_Speak", text). Runs on the sender's thread.
		class SpeakEventSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event,
				RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
			{
				if (a_event && a_event->eventName == SPEAK_EVENT) {
					Speaker::Speak(a_event->strArg.c_str(), -1, -1.0f, -1.0f);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		SpeakEventSink g_speakSink;

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
			if (!TtsEngine::IsLoaded()) {
				return "the model is still loading";
			}
			if (g_usableSlots.load() <= 0) {
				return "no playable slot yet (restart the game once)";
			}
			return {};
		}
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

	std::string Status()
	{
		const auto settings = Config::Get();
		if (const auto reason = NotReadyReason(); !reason.empty()) {
			return "not ready: " + reason;
		}
		return std::format("{} | model {} ({} voices, {} Hz) | voice {} | slots {} of {} | sherpa-onnx {}",
			IsEnabled() ? "ready" : "switched off", settings->model, TtsEngine::VoiceCount(),
			TtsEngine::SampleRate(), settings->voice, g_usableSlots.load(), settings->slots,
			TtsEngine::RuntimeVersion());
	}

	std::string Channel()
	{
		return Config::Get()->channel;
	}
}
