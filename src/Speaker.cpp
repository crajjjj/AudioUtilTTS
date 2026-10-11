#include "Speaker.h"

#include "Config.h"
#include "Lip.h"
#include "TtsEngine.h"

#include <API/AudioUtilAPI.h>

#include <Windows.h>

#include <array>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <optional>
#include <thread>
#include <tuple>
#include <utility>

namespace Speaker
{
	namespace
	{
		namespace fs = std::filesystem;
		using Clock = std::chrono::steady_clock;

		constexpr auto MODELS_DIR = "Data\\SKSE\\Plugins\\AudioUtilTTS\\models\\"sv;
		constexpr auto CACHE_DIR = "Data\\SKSE\\Plugins\\AudioUtilTTS\\cache"sv;
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
			kDialogue,
			kLaneCount
		};

		// Where each lane's placeholder files live, relative to Data. A dialogue
		// line is played by the game itself as a voice file, so its placeholders
		// sit where voice files do.
		constexpr std::array<std::string_view, kLaneCount> SLOT_DIR_REL{ "Sound\\AudioUtilTTS"sv,
			"Sound\\AudioUtilTTS"sv, "Sound\\Voice\\AudioUtilTTS"sv };
		constexpr std::array<std::string_view, kLaneCount> SLOT_PREFIX{ "_tts_slot"sv, "_tts_speech"sv,
			"_tts_dialog"sv };
		constexpr std::array<std::string_view, kLaneCount> LANE_NAME{ "narration"sv, "speech"sv, "dialogue"sv };

		constexpr std::size_t MAX_DIALOGUE_WAITING = 96;
		// Greeting lines made ahead may fill this much of the queue and no more:
		// they are a guess, and each costs a line's worth of CPU.
		constexpr std::size_t MAX_PREPARED_WAITING = 12;
		// A dialogue slot handed out this recently may belong to a line that waits
		// in the dialogue menu to be picked: it is not given to another line.
		constexpr auto MIN_SLOT_AGE = std::chrono::seconds(30);
		// A line is written into its slot only while the slot cannot change hands
		// for at least this long yet (see DialogueSlotWritable).
		constexpr auto SLOT_WRITE_MARGIN = std::chrono::seconds(5);
		// A topic arriving this close behind other responses is a topic list being built.
		constexpr auto BURST_GAP = std::chrono::milliseconds(150);
		// A listed line gets a slot only when it should be generated within this
		// long: about how soon a player picks a topic after the menu shows it.
		constexpr float LISTED_READY_MS = 2500.0f;

		// One dialogue line on its way: shared by the game thread that asked for it
		// and the worker that generates it.
		struct DialogueJob
		{
			// set before the job is queued, read-only after
			int           slot = -1;        // -1 = generated for the cache only
			std::uint64_t slotStamp = 0;    // the slot's stamp when it was handed out
			fs::path      cacheFile;
			std::string   cacheKey;         // its file name: what the line is known by
			std::string   label;            // whose voice, for the log
			const void*   info = nullptr;   // the topic info it belongs to: compared, never read
			int           response = 1;     // its number inside that topic info
			bool          urgent = false;   // a game thread is waiting for it
			bool          prepared = false; // made ahead of a greeting (Greetings): behind every line the game asked for
			std::uint64_t order = 0;        // arrival order

			std::mutex              lock;  // guards the block below
			std::condition_variable ready;
			bool                    done = false;        // the worker is finished with it, one way or the other
			bool                    inSlot = false;      // the slot holds this line
			bool                    slotWanted = false;  // cleared once the game went on without it
			float                   seconds = 0.0f;      // length of the generated speech
		};

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
			float           estimateMs = 0.0f;         // expected generation time, 0 = unknown
			std::shared_ptr<DialogueJob> job;          // dialogue lane only
		};

		struct LaneState
		{
			std::vector<bool>       slotUsable;
			std::unordered_set<int> slotsCreated;  // made this session: not indexed until a restart
			int                     nextSlot = 0;
			Clock::time_point       quietAt{};  // when the line now playing on this lane ends
		};

		// What a dialogue slot holds. A dialogue line keeps its slot for as long as
		// the game may still play it, which nothing announces: the game builds the
		// lines of every topic in the dialogue menu up front and plays one much
		// later, or never. So slots go to the line used longest ago, never to one
		// used a moment ago, and a line asked for again gets the slot it has.
		struct DialogueSlot
		{
			std::uint64_t     stamp = 0;  // bumped each time the slot goes to a line
			Clock::time_point used{};     // when a line last asked for it
			std::string       key;        // the line it holds or waits for, "" = none
		};

		// A cached line on its way into a slot (see Filler).
		struct FillJob
		{
			int           slot = -1;
			std::uint64_t slotStamp = 0;
			fs::path      cacheFile;
			float         volume = 1.0f;
			std::string   text;  // seeds the mouth shapes
		};

		std::mutex                        g_lock;  // guards the block below
		std::condition_variable           g_wake;
		std::condition_variable           g_fillWake;
		std::deque<Line>                  g_waiting;
		bool                              g_reloadWanted = false;
		bool                              g_started = false;
		std::string                       g_problem;  // why the default model is not loaded
		std::array<LaneState, kLaneCount> g_lanes;
		std::unordered_set<std::string>   g_unknownPresets;  // already warned about
		Clock::time_point                 g_busyUntil{};     // when the line being generated should be done
		// dialogue: lines waiting to be generated (always ahead of g_waiting) and the slots
		std::vector<Line>                    g_dialogueWaiting;
		std::uint64_t                        g_dialogueOrder = 0;
		std::vector<DialogueSlot>            g_dialogueSlots;
		std::uint64_t                        g_slotStampClock = 0;
		std::unordered_map<std::string, int> g_slotOfKey;      // line -> the slot it has
		std::unordered_set<std::string>      g_generating;     // lines being generated, by cache key
		std::unordered_set<std::string>      g_failedLines;    // lines that could not be generated, by cache key
		std::unordered_set<std::string>      g_unusableModels;  // models that would not load
		std::deque<FillJob>                  g_fills;          // cached lines on their way into slots
		Clock::time_point                    g_lastDialogueEnd{};  // when the last dialogue response was handled
		// per model (normalized folder name), all cleared by a reload:
		std::unordered_map<std::string, float>         g_msPerChar;    // measured generation time per character
		std::unordered_map<std::string, std::uint64_t> g_modelStamps;  // TtsEngine::ModelStamp, 0 = no such model
		std::unordered_set<std::string>                g_loggedOnce;   // see LogOnce

		std::atomic<std::uint64_t> g_epoch{ 1 };  // bumped by Cancel: lines from an older epoch are dropped
		std::atomic<std::uint64_t> g_dialogueEpoch{ 1 };  // the same for dialogue lines, bumped by Reset
		std::atomic<bool>          g_enabled{ true };
		std::atomic<bool>          g_working{ false };
		std::atomic<bool>          g_warnedNotReady{ false };
		std::atomic<bool>          g_warnedOldLipSync{ false };

		// dialogue
		std::atomic<bool>          g_dialogueHooked{ false };  // the game hook is in place
		std::atomic<bool>          g_dialogueOn{ true };       // session switch
		std::atomic<std::uint32_t> g_dialogueGiven{ 0 };      // responses handed a voice file
		std::atomic<std::uint32_t> g_dialogueUnvoiced{ 0 };   // responses left as the game built them
		std::atomic<std::uint32_t> g_dialogueGenerated{ 0 };  // lines generated
		std::atomic<std::uint32_t> g_dialoguePrepared{ 0 };   // of those, greetings made ahead

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

		// ------------------------------------------------------------------ helpers

		// The folder name of the model a line is generated with.
		std::string ModelKeyOf(const Line& a_line, const Config::Settings& a_settings)
		{
			return a_line.model.empty() ? Config::Normalize(a_settings.model) : a_line.model;
		}

		// Voice, volume and speed a line left unset take the configured defaults.
		void FillDefaults(Line& a_line, const Config::Settings& a_settings)
		{
			if (a_line.voice < 0) {
				a_line.voice = a_settings.voice;
			}
			a_line.volume = a_line.volume >= 0.0f ? std::min(a_line.volume, 1.0f) : a_settings.volume;
			if (a_line.speed <= 0.0f) {
				a_line.speed = a_settings.speed;
			}
		}

		// For a state that holds line after line: it goes into the log once.
		void LogOnce(const std::string& a_message)
		{
			bool first = false;
			{
				std::scoped_lock lock{ g_lock };
				if (g_loggedOnce.size() > 2048) {
					g_loggedOnce.clear();
				}
				first = g_loggedOnce.insert(a_message).second;
			}
			if (first) {
				logger::info("{}", a_message);
			}
		}

		// -------------------------------------------------------------------- slots

		fs::path DataRoot() { return fs::current_path() / "Data"; }

		std::string SlotName(Lane a_lane, int a_i)
		{
			return std::format("{}{:02d}.wav", SLOT_PREFIX[a_lane], a_i);
		}
		std::string SlotDataRel(Lane a_lane, int a_i)
		{
			return std::string(SLOT_DIR_REL[a_lane]) + "\\" + SlotName(a_lane, a_i);
		}
		fs::path SlotDisk(Lane a_lane, int a_i)
		{
			return DataRoot() / SLOT_DIR_REL[a_lane] / SlotName(a_lane, a_i);
		}
		// the mouth animation the game reads beside a dialogue slot's wav
		fs::path SlotLipDisk(int a_i)
		{
			return SlotDisk(kDialogue, a_i).replace_extension(".lip");
		}

		// A slot is playable only if the game indexed it at launch, so one created
		// now starts working after the next restart.
		void PrepareSlots(Lane a_lane, int a_count)
		{
			std::error_code ec;
			fs::create_directories(DataRoot() / SLOT_DIR_REL[a_lane], ec);
			if (a_lane == kDialogue) {
				// the game looks for a line's mouth animation when it builds the
				// line, so every dialogue slot has one from the start: a closed mouth
				for (int i = 0; i < a_count; ++i) {
					const auto lip = SlotLipDisk(i);
					if (!fs::exists(lip, ec)) {
						std::string error;
						Lip::Write(lip, Lip::Rest(), error);
					}
				}
			}

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
			if (a_lane == kDialogue) {
				// every line loses its slot; stamps handed out earlier go stale
				g_dialogueSlots.assign(static_cast<std::size_t>(a_count), DialogueSlot{});
				g_slotOfKey.clear();
			}
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

		// The slot a_key already has, or -1. Asking counts as use. g_lock held.
		int ReuseDialogueSlotLocked(const std::string& a_key)
		{
			const auto it = g_slotOfKey.find(a_key);
			if (it == g_slotOfKey.end()) {
				return -1;
			}
			const auto slot = static_cast<std::size_t>(it->second);
			if (slot >= g_dialogueSlots.size() || g_dialogueSlots[slot].key != a_key) {
				g_slotOfKey.erase(it);
				return -1;
			}
			g_dialogueSlots[slot].used = Clock::now();
			return it->second;
		}

		// Gives a_key the slot used longest ago, or -1 when even that one was used
		// within MIN_SLOT_AGE. a_stamp identifies this hand-out: a late writer asks
		// DialogueSlotWritable whether the slot is still its own. g_lock held.
		int TakeDialogueSlotLocked(const std::string& a_key, std::uint64_t& a_stamp)
		{
			const auto& usable = g_lanes[kDialogue].slotUsable;
			int         best = -1;
			for (std::size_t i = 0; i < g_dialogueSlots.size() && i < usable.size(); ++i) {
				if (usable[i] && (best < 0 || g_dialogueSlots[i].used < g_dialogueSlots[static_cast<std::size_t>(best)].used)) {
					best = static_cast<int>(i);
				}
			}
			if (best < 0) {
				return -1;
			}
			auto&      slot = g_dialogueSlots[static_cast<std::size_t>(best)];
			const auto now = Clock::now();
			if (slot.used != Clock::time_point{} && now - slot.used < MIN_SLOT_AGE) {
				return -1;
			}
			if (const auto it = g_slotOfKey.find(slot.key); it != g_slotOfKey.end() && it->second == best) {
				g_slotOfKey.erase(it);
			}
			slot.key = a_key;
			slot.used = now;
			slot.stamp = a_stamp = ++g_slotStampClock;
			g_slotOfKey[a_key] = best;
			return best;
		}

		int TakeDialogueSlot(const std::string& a_key, std::uint64_t& a_stamp)
		{
			std::scoped_lock lock{ g_lock };
			return TakeDialogueSlotLocked(a_key, a_stamp);
		}

		// May a line still be written into the slot it was handed? Only while the
		// slot is its own and cannot go to another line for a while yet. The check
		// and the write are two steps, and the margin is what keeps a slow writer
		// out of a slot that changed hands in between.
		bool DialogueSlotWritable(int a_slot, std::uint64_t a_stamp)
		{
			std::scoped_lock lock{ g_lock };
			if (a_slot < 0 || static_cast<std::size_t>(a_slot) >= g_dialogueSlots.size()) {
				return false;
			}
			const auto& slot = g_dialogueSlots[static_cast<std::size_t>(a_slot)];
			return slot.stamp == a_stamp && Clock::now() - slot.used < MIN_SLOT_AGE - SLOT_WRITE_MARGIN;
		}

		// The line this slot was handed to did not get into it: the next request
		// for that line must not be told it has a slot. The slot keeps its age, as
		// the game may still hold its path. g_lock held.
		void ForgetDialogueSlotLocked(int a_slot, std::uint64_t a_stamp)
		{
			if (a_slot < 0 || static_cast<std::size_t>(a_slot) >= g_dialogueSlots.size()) {
				return;
			}
			auto& slot = g_dialogueSlots[static_cast<std::size_t>(a_slot)];
			if (slot.stamp == a_stamp) {
				if (const auto it = g_slotOfKey.find(slot.key); it != g_slotOfKey.end() && it->second == a_slot) {
					g_slotOfKey.erase(it);
				}
				slot.key.clear();
			}
		}

		void ForgetDialogueSlot(int a_slot, std::uint64_t a_stamp)
		{
			std::scoped_lock lock{ g_lock };
			ForgetDialogueSlotLocked(a_slot, a_stamp);
		}

		// Opened with no sharing: while the bytes go in nobody else can open the
		// file, so the game never reads half a line. If the game holds the file
		// open, this fails and the file keeps what it had.
		bool WriteExclusive(const fs::path& a_path, const void* a_data, std::size_t a_size)
		{
			HANDLE handle = INVALID_HANDLE_VALUE;
			// a virus scanner looking at the file just written holds it for a moment
			for (int attempt = 0; attempt < 3 && handle == INVALID_HANDLE_VALUE; ++attempt) {
				if (attempt > 0) {
					::Sleep(4);
				}
				handle = ::CreateFileW(a_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
					nullptr);
				if (handle == INVALID_HANDLE_VALUE && ::GetLastError() != ERROR_SHARING_VIOLATION) {
					return false;
				}
			}
			if (handle == INVALID_HANDLE_VALUE) {
				return false;
			}
			DWORD      written = 0;
			const bool ok = ::WriteFile(handle, a_data, static_cast<DWORD>(a_size), &written, nullptr) != 0 &&
							written == a_size;
			::CloseHandle(handle);
			return ok;
		}

		// The mouth animation beside a slot's audio. It is rewritten with every
		// line, or the slot would keep the animation of the line it held before.
		void WriteSlotLip(int a_slot, const std::vector<std::uint8_t>& a_lip)
		{
			const auto      lipDisk = SlotLipDisk(a_slot);
			std::error_code ec;
			if (fs::exists(lipDisk, ec) && !WriteExclusive(lipDisk, a_lip.data(), a_lip.size())) {
				logger::warn("Could not write the mouth animation of {}", SlotDataRel(kDialogue, a_slot));
			}
		}

		// Puts a line into a dialogue slot: the audio and the mouth animation.
		bool FillDialogueSlot(int a_slot, const TtsEngine::Audio& a_audio, float a_volume,
			const Config::Settings& a_settings, std::string_view a_text)
		{
			std::vector<char> wav;
			if (a_volume >= 0.0f && a_volume < 0.999f) {
				// the game plays this file, so the preset's volume goes into the samples
				auto scaled = a_audio;
				for (float& sample : scaled.samples) {
					sample *= a_volume;
				}
				wav = TtsEngine::EncodeWav(scaled);
			} else {
				wav = TtsEngine::EncodeWav(a_audio);
			}
			if (wav.empty() || !WriteExclusive(SlotDisk(kDialogue, a_slot), wav.data(), wav.size())) {
				return false;
			}
			WriteSlotLip(a_slot, a_settings.dialogueLip ?
									 Lip::FromAudio(a_audio, a_text, a_settings.dialogueLipStrength) :
									 Lip::Rest());
			return true;
		}

		// What a slot holds until its line is in: silence and a closed mouth, never
		// the line it held before.
		bool FillDialogueSilence(int a_slot, float a_seconds)
		{
			TtsEngine::Audio silence;
			silence.sampleRate = 8000;  // silence needs no more, and the file stays small
			silence.samples.assign(static_cast<std::size_t>(std::clamp(a_seconds, 0.25f, 30.0f) * 8000.0f), 0.0f);
			const auto wav = TtsEngine::EncodeWav(silence);
			if (!WriteExclusive(SlotDisk(kDialogue, a_slot), wav.data(), wav.size())) {
				return false;
			}
			WriteSlotLip(a_slot, Lip::Rest());
			return true;
		}

		// --------------------------------------------------------------------- text

		// One line of plain text: control characters and runs of whitespace become
		// single spaces, and anything past a_maxChars is cut at a sentence end.
		std::string Clean(std::string_view a_text, int a_maxChars)
		{
			const auto  text = TtsEngine::ToSpeakable(a_text);
			std::string out;
			out.reserve(text.size());
			bool space = false;
			for (const char c : text) {
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

		// What a dialogue line says aloud. Markup in <angle brackets> is dropped,
		// and so are stage directions such as (sighs), [laughs] and *coughs* when
		// the config says so. "" when nothing speakable is left.
		std::string CleanDialogue(std::string_view a_text, const Config::Settings& a_settings)
		{
			std::string spoken;
			spoken.reserve(a_text.size());
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				const char c = a_text[i];
				char       closer = 0;
				if (c == '<') {
					closer = '>';
				} else if (a_settings.dialogueSkipDirections) {
					closer = c == '(' ? ')' : c == '[' ? ']' : c == '*' ? '*' : 0;
				}
				// an opener with nothing closing it is ordinary text
				if (const auto end = closer ? a_text.find(closer, i + 1) : std::string_view::npos;
					end != std::string_view::npos) {
					spoken += ' ';
					i = end;
					continue;
				}
				spoken += c;
			}
			auto out = Clean(spoken, a_settings.maxChars);
			const bool speakable = std::any_of(out.begin(), out.end(), [](char a_c) {
				const auto u = static_cast<unsigned char>(a_c);
				return u >= 0x80 || std::isalnum(u) != 0;
			});
			return speakable ? out : std::string{};
		}

		// Roughly how long a line takes to say: 15 characters a second.
		float SpeechMs(std::size_t a_chars)
		{
			return 300.0f + static_cast<float>(a_chars) * 66.0f;
		}

		// ------------------------------------------------------------------- timing

		// How long generating a line should take, from what this model managed
		// before. 0 = not measured yet. g_lock held.
		float EstimateMsLocked(const std::string& a_modelKey, std::size_t a_chars)
		{
			const auto it = g_msPerChar.find(a_modelKey);
			return it == g_msPerChar.end() ? 0.0f : it->second * static_cast<float>(a_chars);
		}

		void NoteSpeed(const std::string& a_modelKey, long long a_ms, std::size_t a_chars)
		{
			if (a_chars == 0) {
				return;
			}
			const float      measured = static_cast<float>(a_ms) / static_cast<float>(a_chars);
			std::scoped_lock lock{ g_lock };
			const auto [it, fresh] = g_msPerChar.try_emplace(a_modelKey, measured);
			if (!fresh) {
				it->second = it->second * 0.7f + measured * 0.3f;
			}
		}

		// Work ahead of a dialogue line queued now: the rest of the line being
		// generated plus the dialogue lines waiting (only the urgent ones when
		// a_urgentOnly: an urgent line jumps the others). Greetings made ahead
		// wait behind every line the game asked for, so they never count.
		// g_lock held.
		float BacklogMsLocked(bool a_urgentOnly)
		{
			float      backlog = 0.0f;
			const auto now = Clock::now();
			if (g_busyUntil > now) {
				backlog += std::chrono::duration<float, std::milli>(g_busyUntil - now).count();
			}
			for (const auto& waiting : g_dialogueWaiting) {
				if (waiting.job->prepared) {
					continue;
				}
				if (!a_urgentOnly || waiting.job->urgent) {
					backlog += waiting.estimateMs;
				}
			}
			return backlog;
		}

		void MarkBusy(float a_estimateMs)
		{
			std::scoped_lock lock{ g_lock };
			g_busyUntil = a_estimateMs > 0.0f ?
							  Clock::now() + std::chrono::milliseconds(static_cast<long long>(a_estimateMs)) :
							  Clock::time_point{};
		}

		// ------------------------------------------------------------------- cache

		// Generated dialogue lines are kept on disk, so a line is generated once
		// and every later time comes from here in a few milliseconds. The file
		// name hashes everything that shapes the audio, the model files included:
		// a retrained model starts with a clean slate by itself.
		fs::path CacheDir() { return fs::current_path() / CACHE_DIR; }

		// 0 = the folder holds no model. Read once per model, until a reload.
		std::uint64_t ModelStampFor(const std::string& a_modelKey)
		{
			{
				std::scoped_lock lock{ g_lock };
				if (const auto it = g_modelStamps.find(a_modelKey); it != g_modelStamps.end()) {
					return it->second;
				}
			}
			const auto stamp =
				Config::IsFolderName(a_modelKey) ? TtsEngine::ModelStamp(std::string(MODELS_DIR) + a_modelKey) : 0;
			std::scoped_lock lock{ g_lock };
			g_modelStamps[a_modelKey] = stamp;
			return stamp;
		}

		// empty = this line is not cached (the model is not there)
		fs::path CacheFileFor(const std::string& a_modelKey, const Line& a_line, const Config::Settings& a_settings)
		{
			const auto stamp = ModelStampFor(a_modelKey);
			if (stamp == 0) {
				return {};
			}
			const auto key = std::format("{:016x}|{}|{:.3f}|{}|{}", stamp, a_line.voice, a_line.speed,
				a_settings.outputRate, a_line.text);
			return CacheDir() / std::format("{:016x}.wav", TtsEngine::Hash64(key));
		}

		bool LoadCached(const fs::path& a_file, TtsEngine::Audio& a_out)
		{
			if (!TtsEngine::ReadWav(a_file, a_out)) {
				return false;
			}
			// the write time doubles as "last used": the trim drops the oldest
			std::error_code ec;
			fs::last_write_time(a_file, fs::file_time_type::clock::now(), ec);
			return true;
		}

		void StoreCached(const fs::path& a_file, const TtsEngine::Audio& a_audio)
		{
			std::error_code ec;
			fs::create_directories(a_file.parent_path(), ec);
			// written under another name first: a crash mid-write must not leave half
			// a line behind under the real one
			auto part = a_file;
			part += ".part";
			std::string error;
			if (!TtsEngine::WriteWav(part, a_audio, error)) {
				logger::warn("Could not cache a dialogue line: {}", error);
				return;
			}
			fs::rename(part, a_file, ec);
			if (ec) {
				fs::remove(part, ec);
			}
		}

		// Keeps the cache under [dialogue] cache_max_mb by dropping the lines used
		// longest ago. 0 = no limit.
		void TrimCache(int a_maxMb)
		{
			struct Entry
			{
				fs::path           path;
				std::uintmax_t     size;
				fs::file_time_type used;
			};
			std::vector<Entry> entries;
			std::uintmax_t     total = 0;
			std::error_code    ec;
			for (const auto& item : fs::directory_iterator(CacheDir(), ec)) {
				if (!item.is_regular_file(ec)) {
					continue;
				}
				if (item.path().extension() != ".wav") {
					fs::remove(item.path(), ec);  // a leftover .part
					continue;
				}
				// a file that went away or is locked this instant is not counted: its
				// error value would read as an enormous size
				const auto size = item.file_size(ec);
				const auto used = ec ? fs::file_time_type{} : item.last_write_time(ec);
				if (ec) {
					ec.clear();
					continue;
				}
				entries.push_back({ item.path(), size, used });
				total += size;
			}
			const auto cap = static_cast<std::uintmax_t>(a_maxMb) * 1024 * 1024;
			if (a_maxMb > 0 && total > cap) {
				std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.used < b.used; });
				std::size_t removed = 0;
				// down to 90%, so the next session does not trim again right away
				for (const auto& entry : entries) {
					if (total <= cap / 10 * 9) {
						break;
					}
					if (fs::remove(entry.path, ec)) {
						total -= std::min(total, entry.size);
						++removed;
					}
				}
				logger::info("Dialogue cache trimmed: {} of {} lines removed", removed, entries.size());
			}
			logger::info("Dialogue cache: {:.1f} MB", static_cast<double>(total) / (1024.0 * 1024.0));
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
					logger::error("Model '{}' not loaded: {}. Narration and character lines asking for it use the "
								  "default model; dialogue lines in that voice stay unvoiced.",
						key, error);
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

		// Marks a dialogue job as over with no result and wakes a game thread that
		// waits on it. Never with g_lock held: a job's lock is always taken first.
		void AbandonJob(const std::shared_ptr<DialogueJob>& a_job)
		{
			{
				std::scoped_lock lock{ a_job->lock };
				a_job->done = true;
			}
			a_job->ready.notify_all();
		}

		// A dialogue line: generated, kept in the cache, and written into its slot
		// when the game may still play that slot. A game thread may be waiting on
		// the job, so every way out marks it done.
		void ProcessDialogue(Line a_line)
		{
			const auto job = a_line.job;
			// after the job is marked done: wake a waiting game thread, tidy the books
			const auto settle = [&](bool a_inSlot) {
				job->ready.notify_all();
				if (a_inSlot) {
					g_dialogueGiven.fetch_add(1);
				}
				std::scoped_lock lock{ g_lock };
				g_generating.erase(job->cacheKey);
				if (!a_inSlot) {
					// whoever asks for this line next must not be told it has a slot
					ForgetDialogueSlotLocked(job->slot, job->slotStamp);
				}
			};
			const auto finish = [&](bool a_inSlot, float a_seconds) {
				{
					std::scoped_lock lock{ job->lock };
					job->inSlot = a_inSlot;
					job->seconds = a_seconds;
					job->done = true;
				}
				settle(a_inSlot);
			};
			const auto current = [&] { return a_line.epoch == g_dialogueEpoch.load(); };
			if (!current()) {
				return finish(false, 0.0f);
			}
			const auto settings = Config::Get();
			const auto modelKey = ModelKeyOf(a_line, *settings);

			TtsEngine::Audio audio;
			long long        synthMs = -1;  // stays -1 when the line turned up in the cache
			// the same line may have been asked for twice and generated meanwhile
			if (!LoadCached(job->cacheFile, audio)) {
				bool        fellBack = false;
				const auto* model = ModelFor(a_line.model, *settings, fellBack);
				if (!model || fellBack) {
					// The line was meant for one voice; the default one in its place
					// would be the wrong speaker, so it stays unvoiced. The game
					// threads are told, so no more slots go to lines of this model.
					{
						std::scoped_lock lock{ g_lock };
						g_unusableModels.insert(modelKey);
					}
					return finish(false, 0.0f);
				}
				if (!current()) {
					return finish(false, 0.0f);
				}
				MarkBusy(a_line.estimateMs);
				std::string error;
				const auto  start = Clock::now();
				const auto  result =
					model->Synthesize(a_line.text, a_line.voice, a_line.speed, current, audio, error);
				MarkBusy(0.0f);
				if (result != TtsEngine::Result::Ok) {
					if (result != TtsEngine::Result::Cancelled) {
						logger::warn("Could not generate a {}-char dialogue line: {}", a_line.text.size(), error);
						// not tried again each time the game builds the line
						std::scoped_lock lock{ g_lock };
						g_failedLines.insert(job->cacheKey);
					}
					return finish(false, 0.0f);
				}
				synthMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
				NoteSpeed(modelKey, synthMs, a_line.text.size());
				TtsEngine::Resample(audio, settings->outputRate);
				StoreCached(job->cacheFile, audio);
				g_dialogueGenerated.fetch_add(1);
			}

			bool inSlot = false;
			{
				// under the job's lock: a game thread gives up on the slot under the
				// same lock, so it either gets the whole line or none of it
				std::scoped_lock lock{ job->lock };
				if (job->slotWanted && current() && DialogueSlotWritable(job->slot, job->slotStamp)) {
					inSlot = FillDialogueSlot(job->slot, audio, a_line.volume, *settings, a_line.text);
				}
				job->inSlot = inSlot;
				job->seconds = audio.Seconds();
				job->done = true;
			}
			settle(inSlot);
			if (synthMs >= 0) {
				if (job->prepared) {
					g_dialoguePrepared.fetch_add(1);
				}
				logger::info("Dialogue line generated in {} ms: {} chars as {:.1f} s of speech ({}), {}", synthMs,
					a_line.text.size(), audio.Seconds(), job->label,
					inSlot          ? "in " + SlotName(kDialogue, job->slot) :
					job->prepared ? "a greeting made ahead"s :
									"kept for the next time it is said"s);
			}
		}

		// The topic info the game is speaking right now, as the dialogue menu
		// reports it. Compared against waiting lines, never read through.
		const void* TopicBeingSaid()
		{
			const auto* topics = RE::MenuTopicManager::GetSingleton();
			return topics ? topics->currentTopicInfo : nullptr;
		}

		// True while the dialogue menu is up. What the game builds then is mostly
		// the menu's topic list: the responses of every topic shown, built whole
		// and long before one of them is said.
		bool DialogueMenuOpen()
		{
			const auto* topics = RE::MenuTopicManager::GetSingleton();
			return topics && topics->menuOpen;
		}

		// The order dialogue lines are generated in, smallest first: one a game
		// thread is waiting for, then the lines of the topic being spoken, then
		// first responses before later ones (a first response is what plays when a
		// topic is picked), then the oldest. Greetings made ahead come after all
		// of those: the game has not asked for them.
		auto RankOf(const DialogueJob& a_job, const void* a_saying)
		{
			return std::tuple(a_job.prepared, !a_job.urgent, !(a_saying && a_job.info == a_saying), a_job.response,
				a_job.order);
		}

		// g_lock held, g_dialogueWaiting not empty
		Line TakeDialogueLocked()
		{
			const void* saying = TopicBeingSaid();
			auto        best = g_dialogueWaiting.begin();
			for (auto it = best + 1; it != g_dialogueWaiting.end(); ++it) {
				if (RankOf(*it->job, saying) < RankOf(*best->job, saying)) {
					best = it;
				}
			}
			Line line = std::move(*best);
			g_dialogueWaiting.erase(best);
			return line;
		}

		// Dialogue or narration next? A dialogue line the game may want any moment
		// goes first: one a game thread waits for, a first response, a line of the
		// topic being spoken. The later responses of topics that are only listed
		// can take a long while to get through and may never be said, so narration
		// and character lines are not kept waiting behind those. g_lock held, one
		// of the two queues not empty.
		bool DialogueGoesFirstLocked()
		{
			if (g_dialogueWaiting.empty() || g_waiting.empty()) {
				return !g_dialogueWaiting.empty();
			}
			const void* saying = TopicBeingSaid();
			return std::any_of(g_dialogueWaiting.begin(), g_dialogueWaiting.end(), [&](const Line& a_line) {
				const auto& job = *a_line.job;
				// a greeting made ahead is a guess: narration is not kept waiting for it
				return !job.prepared && (job.urgent || job.response <= 1 || (saying && job.info == saying));
			});
		}

		// The work that comes before a listed line with this response number: the
		// rest of the line being generated and the waiting lines that rank ahead
		// of it. g_lock held.
		float AheadMsLocked(int a_response)
		{
			float      ahead = 0.0f;
			const auto now = Clock::now();
			if (g_busyUntil > now) {
				ahead += std::chrono::duration<float, std::milli>(g_busyUntil - now).count();
			}
			for (const auto& waiting : g_dialogueWaiting) {
				if (!waiting.job->prepared && (waiting.job->urgent || waiting.job->response <= a_response)) {
					ahead += waiting.estimateMs;
				}
			}
			return ahead;
		}

		// Puts cached lines into their slots, off the game thread. The dialogue
		// menu's whole list is built in one go; reading, converting and writing
		// every cached line there and then would stall the game. Its own thread,
		// so a cached line never waits behind one that is being generated.
		void Filler()
		{
			for (;;) {
				FillJob fill;
				{
					std::unique_lock lock{ g_lock };
					g_fillWake.wait(lock, [] { return !g_fills.empty(); });
					fill = std::move(g_fills.front());
					g_fills.pop_front();
				}
				const auto       settings = Config::Get();
				TtsEngine::Audio audio;
				bool             inSlot = false;
				if (!LoadCached(fill.cacheFile, audio)) {
					// unreadable: removed, so the next request generates the line again
					std::error_code ec;
					fs::remove(fill.cacheFile, ec);
				} else if (DialogueSlotWritable(fill.slot, fill.slotStamp)) {
					inSlot = FillDialogueSlot(fill.slot, audio, fill.volume, *settings, fill.text);
				}
				if (inSlot) {
					g_dialogueGiven.fetch_add(1);
				} else {
					ForgetDialogueSlot(fill.slot, fill.slotStamp);
				}
			}
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

			MarkBusy(a_line.estimateMs);
			TtsEngine::Audio audio;
			std::string      error;
			const auto       start = Clock::now();
			const auto result = model->Synthesize(a_line.text, a_line.voice, a_line.speed, current, audio, error);
			MarkBusy(0.0f);
			if (result == TtsEngine::Result::Cancelled) {
				return;
			}
			if (result != TtsEngine::Result::Ok) {
				logger::warn("Could not speak a {}-char line: {}", a_line.text.size(), error);
				return;
			}
			const auto synthMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
			NoteSpeed(ModelKeyOf(a_line, *settings), synthMs, a_line.text.size());
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
				while (!settings->interrupt) {
					// let the line now playing on this lane finish; a Cancel wakes this
					// early, and so does a dialogue line, which must not sit behind
					// this pause: it is generated now, then the pause goes on
					const auto until = g_lanes[lane].quietAt;
					g_wake.wait_until(lock, until, [&] { return !current() || !g_dialogueWaiting.empty(); });
					// the pause is over once its time is up, whatever dialogue still waits
					if (!current() || g_dialogueWaiting.empty() || Clock::now() >= until) {
						break;
					}
					Line next = TakeDialogueLocked();
					lock.unlock();
					ProcessDialogue(std::move(next));
					lock.lock();
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
			TrimCache(Config::Get()->dialogueCacheMb);
			for (;;) {
				Line line;
				bool dialogue = false;
				{
					std::unique_lock lock{ g_lock };
					g_wake.wait(lock,
						[] { return g_reloadWanted || !g_waiting.empty() || !g_dialogueWaiting.empty(); });
					if (g_reloadWanted) {
						g_reloadWanted = false;
						lock.unlock();
						LoadDefaultModel();
						TrimCache(Config::Get()->dialogueCacheMb);
						continue;
					}
					dialogue = DialogueGoesFirstLocked();
					if (dialogue) {
						line = TakeDialogueLocked();
					} else {
						line = std::move(g_waiting.front());
						g_waiting.pop_front();
						g_working.store(true);  // IsBusy is about narration and character lines
					}
				}
				if (dialogue) {
					ProcessDialogue(std::move(line));
				} else {
					Process(std::move(line));
				}
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
			FillDefaults(a_line, *settings);
			{
				std::scoped_lock lock{ g_lock };
				a_line.epoch = g_epoch.load();
				a_line.estimateMs = EstimateMsLocked(ModelKeyOf(a_line, *settings), a_line.text.size());
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

		// ------------------------------------------------------------- voice types

		// The voice of a voice type, looked for in this order:
		//   1. its line in [voicetype], naming a preset ("" = never spoken for)
		//   2. a preset named like the voice type
		//   3. a model folder named like the voice type (its first voice)
		//   4. when a_fallback: the preset "female" / "male" by sex, else the
		//      default voice
		struct VoiceChoice
		{
			enum Source
			{
				kNone,    // nothing matched and no fallback was allowed
				kSilent,  // the config says this voice type is never spoken for
				kOwn,     // steps 1 to 3: a voice meant for this voice type
				kBySex    // step 4
			};
			Line        line;
			Source      source = kNone;
			std::string label;  // "MaleNord: preset 'malenord'", for the log and the console
		};

		VoiceChoice VoiceForType(const Config::Settings& a_settings, const RE::BGSVoiceType* a_type, bool a_female,
			bool a_fallback)
		{
			VoiceChoice choice;
			// the editor ID is read off the form's own field: the virtual getter can
			// hand back a null pointer
			const char*       rawId = a_type ? a_type->formEditorID.c_str() : nullptr;
			const std::string name = rawId ? rawId : "";
			const auto        id = Config::Normalize(name);
			choice.label = name.empty() ? "no voice type"s : name;

			if (!id.empty()) {
				auto presetKey = id;
				bool mapped = false;
				if (const auto it = a_settings.voiceTypes.find(id); it != a_settings.voiceTypes.end()) {
					if (it->second.empty()) {
						choice.source = VoiceChoice::kSilent;
						choice.label += ": never spoken for";
						return choice;
					}
					presetKey = it->second;
					mapped = true;
				}
				if (auto found = FromPreset(a_settings, presetKey)) {
					choice.line = std::move(*found);
					choice.source = VoiceChoice::kOwn;
					choice.label += std::format(": preset '{}'", presetKey);
					return choice;
				}
				if (mapped) {
					WarnUnknownPreset(presetKey);
				}
				if (ModelStampFor(id) != 0) {
					choice.line.model = id == Config::Normalize(a_settings.model) ? std::string{} : id;
					choice.line.voice = 0;
					choice.source = VoiceChoice::kOwn;
					choice.label += std::format(": model folder '{}'", id);
					return choice;
				}
			}
			if (a_fallback) {
				const auto key = a_female ? "female"s : "male"s;
				if (auto found = FromPreset(a_settings, key)) {
					choice.line = std::move(*found);
					choice.label += std::format(": preset '{}' by sex", key);
				} else {
					choice.label += ": the default voice";
				}
				choice.source = VoiceChoice::kBySex;
			}
			return choice;
		}

		const RE::BGSVoiceType* VoiceTypeOf(RE::Actor* a_actor, bool& a_female)
		{
			auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
			a_female = base && base->IsFemale();
			if (!base) {
				return nullptr;
			}
			if (const auto* type = base->voiceType) {
				return type;
			}
			const auto* race = a_actor->GetRace();
			return race ? race->defaultVoiceTypes[a_female ? RE::SEXES::kFemale : RE::SEXES::kMale] : nullptr;
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
		PrepareSlots(kDialogue, settings->dialogueSlots);
		if (!ResolvePlayFile()) {
			logger::error("AudioUtil.dll is not loaded: narration and character lines cannot be spoken. Install "
						  "AudioUtil 0.9.9 or newer.");
		} else {
			logger::info("AudioUtil found (version {})", g_audioUtilVersion.load());
		}

		std::scoped_lock lock{ g_lock };
		if (!g_started) {
			g_started = true;
			if (auto* source = SKSE::GetModCallbackEventSource()) {
				source->AddEventSink(&g_speakSink);
			}
			// never joined: they live for the whole process, like AudioUtil's tickers
			std::thread(Worker).detach();
			std::thread(Filler).detach();
		}
	}

	bool Reload()
	{
		const bool parsed = Config::Load();
		Reset();
		const auto settings = Config::Get();
		PrepareSlots(kNarration, settings->slots);
		PrepareSlots(kSpeech, settings->speechSlots);
		PrepareSlots(kDialogue, settings->dialogueSlots);
		{
			std::scoped_lock lock{ g_lock };
			g_reloadWanted = true;
			g_unknownPresets.clear();
			// models may have been swapped on disk: measure and stamp them afresh
			g_msPerChar.clear();
			g_modelStamps.clear();
			g_unusableModels.clear();
			g_failedLines.clear();
			g_loggedOnce.clear();
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

	void Reset()
	{
		Cancel();
		std::vector<std::shared_ptr<DialogueJob>> dropped;
		{
			std::scoped_lock lock{ g_lock };
			g_dialogueEpoch.fetch_add(1);
			for (auto& waiting : g_dialogueWaiting) {
				dropped.push_back(std::move(waiting.job));
			}
			g_dialogueWaiting.clear();
			g_generating.clear();
			g_fills.clear();
			// the lines on their way will not come: no line has a slot any more,
			// so each is asked for afresh
			g_slotOfKey.clear();
			for (auto& slot : g_dialogueSlots) {
				slot.key.clear();
			}
		}
		// a game thread may be waiting on one of them
		for (const auto& job : dropped) {
			AbandonJob(job);
		}
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
			// no preset named: the voice of the speaker's voice type, else by their sex
			bool        female = false;
			const auto* type = VoiceTypeOf(a_actor, female);
			auto        choice = VoiceForType(*settings, type, female, true);
			if (choice.source == VoiceChoice::kSilent) {
				return false;
			}
			line = std::move(choice.line);
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
			Reset();
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
						   "narration slots {} of {} | speech slots {} of {}, lipsync {} | dialogue {} | sherpa-onnx {}",
			IsEnabled() ? "ready" : "switched off", settings->model, g_defaultVoices.load(), g_defaultRate.load(),
			settings->voice, settings->presets.size(), g_modelsLoaded.load(), g_usableSlots[kNarration].load(),
			settings->slots, g_usableSlots[kSpeech].load(), settings->speechSlots,
			lipSync ? "on" : (settings->speechLipSync ? "off (AudioUtil older than 0.9.26)" : "off"),
			IsDialogueEnabled() ? "on" : "off", TtsEngine::RuntimeVersion());
	}

	// ------------------------------------------------------------------ dialogue

	void SetDialogueHooked(bool a_hooked)
	{
		g_dialogueHooked.store(a_hooked);
	}

	void SetDialogueEnabled(bool a_enabled)
	{
		g_dialogueOn.store(a_enabled);
	}

	bool IsDialogueEnabled()
	{
		const auto settings = Config::Get();
		return g_dialogueHooked.load() && g_dialogueOn.load() && g_enabled.load() && settings->enable &&
			   settings->dialogue;
	}

	std::string VoiceDialogue(std::string_view a_text, const RE::BGSVoiceType* a_voiceType, int a_response,
		const void* a_info)
	{
		// Per thread, for the topic info whose responses are being built on it:
		// the speech time of its earlier responses (how long a later one has
		// before the game reaches it), how long the game has been kept waiting,
		// and whether it is only listed (see below).
		thread_local const void* t_info = nullptr;
		thread_local int         t_response = 0;
		thread_local float       t_leadMs = 0.0f;
		thread_local float       t_waitedMs = 0.0f;
		thread_local bool        t_listed = false;
		// A new topic info, or the same one built again. Told by the info, not by
		// "response number 1": a response with a recorded voice never gets here,
		// so the first one that does may well be the second.
		const bool start = a_info != t_info || a_response <= t_response;
		t_info = a_info;
		t_response = a_response;
		if (!a_voiceType || !IsDialogueEnabled()) {
			return {};
		}

		// About to be said, or only listed? The game builds the responses of
		// every topic in the dialogue menu when it fills the menu, and says one
		// when the player picks it, seconds later or never. The game is never
		// made to wait for a listed line. Two signs of a list: the dialogue menu
		// is open, or a topic arrives right behind other responses.
		if (start) {
			t_leadMs = 0.0f;
			t_waitedMs = 0.0f;
			std::scoped_lock lock{ g_lock };
			t_listed = g_lastDialogueEnd != Clock::time_point{} && Clock::now() - g_lastDialogueEnd < BURST_GAP;
		}
		const bool listed = t_listed || DialogueMenuOpen();
		struct NoteEnd
		{
			~NoteEnd()
			{
				std::scoped_lock lock{ g_lock };
				g_lastDialogueEnd = Clock::now();
			}
		} noteEnd;

		const auto settings = Config::Get();
		auto       text = CleanDialogue(a_text, *settings);
		if (text.empty()) {
			return {};
		}
		const bool        first = a_response <= 1;  // the response said the moment its topic is
		const std::size_t chars = text.size();
		const float       speechMs = SpeechMs(chars);

		// The response stays as the game built it: unvoiced, and Fuz Ro D'oh's to
		// cover. A reason that holds line after line is logged once, a reason
		// already logged for this very line not again.
		enum class Log
		{
			kLine,
			kOnce,
			kNo
		};
		const auto leave = [&](std::string_view a_label, std::string_view a_why, Log a_log = Log::kLine) {
			t_leadMs += speechMs;
			g_dialogueUnvoiced.fetch_add(1);
			if (a_log == Log::kOnce) {
				LogOnce(std::format("Dialogue lines stay unvoiced ({}): {}", a_label, a_why));
			} else if (a_log == Log::kLine) {
				logger::info("Dialogue line left unvoiced ({} chars, {}): {}", chars, a_label, a_why);
			}
			return std::string{};
		};

		const bool female = a_voiceType->data.flags.any(RE::VOICE_TYPE_DATA::Flag::kFemale);
		auto       choice = VoiceForType(*settings, a_voiceType, female, settings->dialogueFallback);
		const auto label = choice.label;
		if (choice.source == VoiceChoice::kSilent) {
			return leave(label, "the config says so", Log::kOnce);
		}
		if (choice.source == VoiceChoice::kNone) {
			return leave(label, "no voice is set up for it and [dialogue] fallback is off", Log::kOnce);
		}
		if (!g_defaultLoaded.load()) {
			return leave("every voice", "the default model is not loaded", Log::kOnce);
		}
		if (g_usableSlots[kDialogue].load() <= 0) {
			return leave("every voice", "no playable dialogue slot yet (restart the game once)", Log::kOnce);
		}

		Line line = std::move(choice.line);
		line.text = std::move(text);
		line.lane = kDialogue;
		FillDefaults(line, *settings);
		const auto modelKey = ModelKeyOf(line, *settings);
		const auto cacheFile = CacheFileFor(modelKey, line, *settings);
		if (cacheFile.empty()) {
			return leave(label, std::format("the model folder '{}' holds no model", modelKey), Log::kOnce);
		}
		// The file name is plain hex. The folder it sits in is the game's own and
		// may hold characters that do not convert to a narrow string at all.
		const auto cacheKey = cacheFile.filename().string();
		// what a slot holds: the cached line at this volume
		const auto key = std::format("{}|{:.2f}", cacheKey, line.volume);
		const auto slotPath = [](int a_slot) { return "Data\\" + SlotDataRel(kDialogue, a_slot); };

		// 1. the line has a slot already, filled or being filled
		{
			bool unusable = false;
			bool failed = false;
			int  held = -1;
			{
				std::scoped_lock lock{ g_lock };
				unusable = g_unusableModels.contains(modelKey);
				failed = g_failedLines.contains(cacheKey);
				if (!unusable && !failed) {
					held = ReuseDialogueSlotLocked(key);
				}
			}
			if (unusable) {
				return leave(label, std::format("the model '{}' could not be loaded (see above)", modelKey), Log::kOnce);
			}
			if (failed) {
				return leave(label, "it could not be generated", Log::kNo);
			}
			if (held >= 0) {
				t_leadMs += speechMs;
				return slotPath(held);
			}
		}

		// 2. said before: it is in the cache
		if (std::error_code ec; fs::exists(cacheFile, ec)) {
			std::uint64_t stamp = 0;
			const int     slot = TakeDialogueSlot(key, stamp);
			if (slot < 0) {
				return leave(label, "every placeholder file is held by a recent line");
			}
			if (listed) {
				// Put in place off the game thread: a menu lists many lines at once.
				// Until then the slot holds silence, never the line it held before.
				if (!FillDialogueSilence(slot, 0.25f)) {
					ForgetDialogueSlot(slot, stamp);
					return leave(label, "its placeholder file could not be written");
				}
				{
					std::scoped_lock lock{ g_lock };
					g_fills.push_back(FillJob{ slot, stamp, cacheFile, line.volume, line.text });
				}
				g_fillWake.notify_one();
				t_leadMs += speechMs;
				return slotPath(slot);
			}
			// about to be said: it has to be in place before the game is told
			if (TtsEngine::Audio cached; LoadCached(cacheFile, cached)) {
				if (!FillDialogueSlot(slot, cached, line.volume, *settings, line.text)) {
					ForgetDialogueSlot(slot, stamp);
					return leave(label, "its placeholder file could not be written");
				}
				t_leadMs += cached.Seconds() * 1000.0f;
				g_dialogueGiven.fetch_add(1);
				return slotPath(slot);
			}
			// unreadable: generated again below
			ForgetDialogueSlot(slot, stamp);
			fs::remove(cacheFile, ec);
		}

		// 3. new: can the game have it this time?
		float estimate = 0.0f;
		float urgentAhead = 0.0f;
		float allAhead = 0.0f;
		float rankAhead = 0.0f;
		{
			std::scoped_lock lock{ g_lock };
			estimate = EstimateMsLocked(modelKey, chars);
			urgentAhead = BacklogMsLocked(true);
			allAhead = BacklogMsLocked(false);
			rankAhead = AheadMsLocked(a_response);
		}
		const float budget = static_cast<float>(settings->dialogueWaitMs) - t_waitedMs;
		bool        later = false;  // it gets a slot now and is put into it when ready
		bool        wait = false;   // the game thread waits for it
		if (listed) {
			// Only when it should be in place by the time a player can pick it. A
			// slot handed out too early plays as silence, and then nothing else
			// covers the line; left alone, it is at least Fuz Ro D'oh's.
			later = rankAhead + estimate <= LISTED_READY_MS + (first ? 0.0f : t_leadMs);
		} else {
			// earlier responses of its topic are still to be said: there is time
			later = !first && allAhead + (estimate > 0.0f ? estimate : 1500.0f) + 250.0f <= t_leadMs;
			// otherwise the game thread may wait a moment, unless this model is
			// known to need longer than that
			wait = !later && budget >= 20.0f && (estimate <= 0.0f || urgentAhead + estimate <= budget);
		}

		auto job = std::make_shared<DialogueJob>();
		job->cacheFile = cacheFile;
		job->cacheKey = cacheKey;
		job->label = label;
		job->info = a_info;
		job->response = a_response;
		if (later || wait) {
			job->slot = TakeDialogueSlot(key, job->slotStamp);
		}
		if (job->slot >= 0 && later && !FillDialogueSilence(job->slot, speechMs / 1000.0f)) {
			ForgetDialogueSlot(job->slot, job->slotStamp);
			job->slot = -1;
		}
		const int           slot = job->slot;
		const std::uint64_t stamp = job->slotStamp;
		job->slotWanted = slot >= 0;
		job->urgent = wait && slot >= 0;
		line.job = job;
		line.estimateMs = estimate;

		bool                                      queued = false;
		bool                                      onItsWay = false;
		std::vector<std::shared_ptr<DialogueJob>> dropped;
		{
			std::scoped_lock lock{ g_lock };
			if (slot < 0 && g_generating.contains(cacheKey)) {
				onItsWay = true;  // for the cache only, and already being generated for it
			} else {
				job->order = ++g_dialogueOrder;
				line.epoch = g_dialogueEpoch.load();
				g_generating.insert(cacheKey);
				g_dialogueWaiting.push_back(std::move(line));
				queued = true;
				if (g_dialogueWaiting.size() > MAX_DIALOGUE_WAITING) {
					// the line that would be generated last makes room, this one included
					const void* saying = TopicBeingSaid();
					auto        worst = g_dialogueWaiting.end();
					for (auto it = g_dialogueWaiting.begin(); it != g_dialogueWaiting.end(); ++it) {
						if (!it->job->urgent && (worst == g_dialogueWaiting.end() ||
													RankOf(*worst->job, saying) < RankOf(*it->job, saying))) {
							worst = it;
						}
					}
					if (worst != g_dialogueWaiting.end()) {
						queued = worst->job != job;
						ForgetDialogueSlotLocked(worst->job->slot, worst->job->slotStamp);
						g_generating.erase(worst->job->cacheKey);
						dropped.push_back(worst->job);
						g_dialogueWaiting.erase(worst);
					}
				}
			}
		}
		for (const auto& gone : dropped) {
			AbandonJob(gone);
		}
		if (!queued && !onItsWay) {
			return leave(label, "too many dialogue lines are waiting");
		}
		g_wake.notify_all();

		if (slot < 0) {
			return leave(label, "it cannot be in place in time, it is being generated for the next time",
				onItsWay ? Log::kNo : Log::kLine);
		}
		if (later) {
			// the worker reports it when it is in
			t_leadMs += speechMs;
			return slotPath(slot);
		}

		const auto       waitStart = Clock::now();
		std::unique_lock lock{ job->lock };
		job->ready.wait_for(lock, std::chrono::milliseconds(static_cast<long long>(budget)), [&] { return job->done; });
		t_waitedMs += std::chrono::duration<float, std::milli>(Clock::now() - waitStart).count();
		if (job->done && job->inSlot) {
			t_leadMs += job->seconds * 1000.0f;
			return slotPath(slot);
		}
		// the game goes on without it; the worker still finishes it for the cache
		job->slotWanted = false;
		const bool over = job->done;
		lock.unlock();
		ForgetDialogueSlot(slot, stamp);
		return leave(label, over ? "it could not be generated or put in place" :
								   "it was not ready in time, it is being generated for the next time");
	}

	const RE::BGSVoiceType* DialogueVoiceType(RE::Actor* a_actor)
	{
		if (!a_actor || !IsDialogueEnabled()) {
			return nullptr;
		}
		const auto  settings = Config::Get();
		bool        female = false;
		const auto* type = VoiceTypeOf(a_actor, female);
		if (!type) {
			return nullptr;
		}
		const auto source = VoiceForType(*settings, type, female, settings->dialogueFallback).source;
		return source == VoiceChoice::kOwn || source == VoiceChoice::kBySex ? type : nullptr;
	}

	bool CanPrepareDialogue(int a_lines)
	{
		std::scoped_lock lock{ g_lock };
		const auto       waitingAhead = static_cast<std::size_t>(std::count_if(g_dialogueWaiting.begin(),
			  g_dialogueWaiting.end(), [](const Line& a_line) { return a_line.job->prepared; }));
		const auto       wanted = static_cast<std::size_t>(std::max(a_lines, 1));
		return waitingAhead + wanted <= MAX_PREPARED_WAITING && g_dialogueWaiting.size() + wanted <= MAX_DIALOGUE_WAITING;
	}

	bool PrepareDialogue(std::string_view a_text, const RE::BGSVoiceType* a_voiceType, int a_response,
		const void* a_info)
	{
		if (!a_voiceType || !IsDialogueEnabled() || !g_defaultLoaded.load()) {
			return false;
		}
		const auto settings = Config::Get();
		auto       text = CleanDialogue(a_text, *settings);
		if (text.empty()) {
			return false;
		}
		const bool female = a_voiceType->data.flags.any(RE::VOICE_TYPE_DATA::Flag::kFemale);
		auto       choice = VoiceForType(*settings, a_voiceType, female, settings->dialogueFallback);
		if (choice.source != VoiceChoice::kOwn && choice.source != VoiceChoice::kBySex) {
			return false;
		}

		Line line = std::move(choice.line);
		line.text = std::move(text);
		line.lane = kDialogue;
		FillDefaults(line, *settings);
		const auto modelKey = ModelKeyOf(line, *settings);
		const auto cacheFile = CacheFileFor(modelKey, line, *settings);
		if (cacheFile.empty()) {
			return false;
		}
		if (std::error_code ec; fs::exists(cacheFile, ec)) {
			return false;  // said or made before
		}
		const auto cacheKey = cacheFile.filename().string();

		auto job = std::make_shared<DialogueJob>();
		job->cacheFile = cacheFile;
		job->cacheKey = cacheKey;
		job->label = choice.label;
		job->info = a_info;
		job->response = a_response;
		job->prepared = true;
		line.job = job;
		{
			std::scoped_lock lock{ g_lock };
			if (g_unusableModels.contains(modelKey) || g_failedLines.contains(cacheKey) ||
				g_generating.contains(cacheKey)) {
				return false;
			}
			const auto waitingAhead = std::count_if(g_dialogueWaiting.begin(), g_dialogueWaiting.end(),
				[](const Line& a_line) { return a_line.job->prepared; });
			if (static_cast<std::size_t>(waitingAhead) >= MAX_PREPARED_WAITING ||
				g_dialogueWaiting.size() >= MAX_DIALOGUE_WAITING) {
				return false;  // the queue is full: this one is left for when it is said
			}
			job->order = ++g_dialogueOrder;
			line.epoch = g_dialogueEpoch.load();
			line.estimateMs = EstimateMsLocked(modelKey, line.text.size());
			g_generating.insert(cacheKey);
			g_dialogueWaiting.push_back(std::move(line));
		}
		g_wake.notify_all();
		return true;
	}

	std::string DescribeVoice(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return "no actor";
		}
		const auto  settings = Config::Get();
		bool        female = false;
		const auto* type = VoiceTypeOf(a_actor, female);
		const auto  choice = VoiceForType(*settings, type, female, true);
		if (choice.source == VoiceChoice::kSilent) {
			return choice.label;
		}
		const auto& line = choice.line;
		return std::format("{} (model {}, voice {}){}", choice.label, line.model.empty() ? settings->model : line.model,
			line.voice < 0 ? settings->voice : line.voice,
			choice.source == VoiceChoice::kBySex && !settings->dialogueFallback ?
				" | unvoiced dialogue stays silent: [dialogue] fallback is off" :
				"");
	}

	std::string DialogueStatus()
	{
		const auto settings = Config::Get();
		std::string state;
		if (!settings->dialogue) {
			state = "off in AudioUtilTTS.toml";
		} else if (!g_dialogueHooked.load()) {
			state = "off: the game hook is not in place (see AudioUtilTTS.log; a change of [dialogue] enable needs "
					"a restart)";
		} else if (!IsDialogueEnabled()) {
			state = "switched off";
		} else {
			state = "on";
		}
		std::uintmax_t  bytes = 0;
		std::size_t     lines = 0;
		std::error_code ec;
		for (const auto& item : fs::directory_iterator(CacheDir(), ec)) {
			if (item.is_regular_file(ec) && item.path().extension() == ".wav") {
				if (const auto size = item.file_size(ec); !ec) {
					bytes += size;
					++lines;
				}
				ec.clear();
			}
		}
		return std::format("dialogue {} | this session: {} responses given a voice, {} left unvoiced, {} lines "
						   "generated ({} of them greetings made ahead, which is {}) | slots {} of {} | lipsync {} | "
						   "wait {} ms | {} voice types mapped | cache {} lines, {:.1f} MB",
			state, g_dialogueGiven.load(), g_dialogueUnvoiced.load(), g_dialogueGenerated.load(),
			g_dialoguePrepared.load(), settings->dialoguePrepare ? "on" : "off", g_usableSlots[kDialogue].load(),
			settings->dialogueSlots, settings->dialogueLip ? "on" : "off", settings->dialogueWaitMs,
			settings->voiceTypes.size(), lines, static_cast<double>(bytes) / (1024.0 * 1024.0));
	}

	int ClearDialogueCache()
	{
		int             removed = 0;
		std::error_code ec;
		for (const auto& item : fs::directory_iterator(CacheDir(), ec)) {
			if (item.is_regular_file(ec) && fs::remove(item.path(), ec)) {
				++removed;
			}
		}
		logger::info("Dialogue cache cleared: {} files removed", removed);
		return removed;
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
