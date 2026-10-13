#include "Greetings.h"

#include "Config.h"
#include "DialogueHook.h"
#include "Speaker.h"

#include <algorithm>
#include <thread>

namespace Greetings
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr float RANGE = 1500.0f;                      // game units around the player (about 20 m)
		constexpr int   LINES_PER_ACTOR = 3;                  // greetings built for one NPC
		constexpr int   INFOS_PER_STEP = 200;                 // topic infos checked in one step on the game thread
		constexpr auto  STEP_EVERY = std::chrono::milliseconds(250);
		constexpr auto  LOOK_EVERY = std::chrono::seconds(2);   // between two NPCs
		constexpr auto  AGAIN_AFTER = std::chrono::minutes(5);  // before the same NPC is looked at again
		constexpr std::size_t SEEN_MAX = 512;

		// One NPC being looked at: where in the topic list the check stands.
		struct Scan
		{
			RE::ActorHandle actor;
			std::size_t     topic = 0;
			std::uint32_t   info = 0;
			int             found = 0;    // greetings that would be said
			int             unvoiced = 0; // their responses that have no voice file
			int             queued = 0;   // of those, lines queued for generation
			int             checked = 0;
			bool            active = false;
		};

		// game thread only
		std::vector<RE::TESTopic*>                        g_topics;  // greeting topics, highest quest priority first
		std::unordered_map<RE::FormID, Clock::time_point> g_seen;    // NPC -> when it was last looked at
		std::unordered_set<std::uint64_t>                 g_built;   // (topic info, voice type) built this session
		Scan                                              g_scan;
		Clock::time_point                                 g_nextLook{};

		std::atomic<bool> g_started{ false };
		std::atomic<bool> g_stepPending{ false };
		std::atomic<bool> g_resetWanted{ false };

		bool Usable(RE::Actor* a_actor, RE::Actor* a_player)
		{
			return a_actor && a_actor != a_player && a_actor->Is3DLoaded() && !a_actor->IsDisabled() &&
				   !a_actor->IsDead() && !a_actor->IsInCombat();
		}

		// The nearest NPC in range that has a voice and was not looked at lately.
		bool PickActor(RE::PlayerCharacter* a_player)
		{
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return false;
			}
			const auto      now = Clock::now();
			const auto      origin = a_player->GetPosition();
			RE::ActorHandle best;
			float           bestDistance = RANGE * RANGE;
			int             inRange = 0;
			int             withVoice = 0;
			for (const auto& handle : lists->highActorHandles) {
				const auto actor = handle.get();
				if (!Usable(actor.get(), a_player)) {
					continue;
				}
				const float distance = origin.GetSquaredDistance(actor->GetPosition());
				if (distance >= RANGE * RANGE) {
					continue;
				}
				++inRange;
				if (!Speaker::DialogueVoiceType(actor.get())) {
					continue;  // their lines stay unvoiced whatever is made ahead
				}
				++withVoice;
				if (distance >= bestDistance) {
					continue;
				}
				if (const auto it = g_seen.find(actor->GetFormID()); it != g_seen.end() && now - it->second < AGAIN_AFTER) {
					continue;
				}
				best = handle;
				bestDistance = distance;
			}
			// The first looks go into the log, and the first one that finds a voice:
			// they show that the check runs and what it sees.
			static int  looksShown = 0;
			static bool voiceShown = false;
			if (looksShown < 3 || (withVoice > 0 && !voiceShown)) {
				++looksShown;
				voiceShown = voiceShown || withVoice > 0;
				logger::info("Greetings: {} NPCs within range, {} of them with a voice of their own", inRange, withVoice);
			}
			const auto actor = best.get();
			if (!actor) {
				return false;
			}
			if (g_seen.size() >= SEEN_MAX) {
				std::erase_if(g_seen, [&](const auto& a_entry) { return now - a_entry.second >= AGAIN_AFTER; });
				if (g_seen.size() >= SEEN_MAX) {
					g_seen.clear();
				}
			}
			g_seen[actor->GetFormID()] = now;
			g_scan = Scan{};
			g_scan.actor = best;
			g_scan.active = true;
			return true;
		}

		// Has the game build the responses of one greeting for this NPC. The hook
		// sees them and queues the unvoiced ones; nothing is said or shown.
		void Build(RE::TESTopic* a_topic, RE::TESTopicInfo* a_info, RE::Actor* a_actor)
		{
			const auto* voiceType = Speaker::DialogueVoiceType(a_actor);
			if (!voiceType) {
				return;
			}
			const auto key = (static_cast<std::uint64_t>(a_info->GetFormID()) << 32) | voiceType->GetFormID();
			if (!g_built.insert(key).second) {
				return;  // built before for this voice type: cached, on its way, or not wanted
			}
			DialogueHook::Preparing preparing;
			// The list of responses is freed with the item, the few small response
			// objects in it are not (CommonLib has no destructor for them). Each
			// greeting is built once per voice type and session, so that stays small.
			RE::DialogueItem item{ a_topic->ownerQuest, a_topic, a_info, a_actor };
			g_scan.unvoiced += preparing.Seen();
			g_scan.queued += preparing.Queued();
		}

		// Greeting topics that have topic infos to check right now.
		std::size_t WithInfos()
		{
			return static_cast<std::size_t>(std::count_if(g_topics.begin(), g_topics.end(),
				[](const RE::TESTopic* a_topic) { return a_topic->topicInfos && a_topic->numTopicInfos > 0; }));
		}

		void Finish()
		{
			// An NPC with nothing to make is the common case and not worth a line,
			// except for the first few: they show in the log that the check runs.
			static int quietShown = 0;
			if (g_scan.queued > 0 || (g_scan.checked > 0 && quietShown < 5)) {
				if (g_scan.queued == 0) {
					++quietShown;
				}
				const auto  actor = g_scan.actor.get();
				const char* name = actor ? actor->GetDisplayFullName() : nullptr;
				logger::info("Greetings made ahead for {}: {} lines queued ({} greetings would be said, {} unvoiced "
							 "responses, {} topic infos checked; {} of {} greeting topics have topic infos)",
					name && *name ? name : "an NPC", g_scan.queued, g_scan.found, g_scan.unvoiced, g_scan.checked,
					WithInfos(), g_topics.size());
			}
			g_scan = Scan{};
		}

		// A slice of the check for the NPC being looked at.
		void Continue(RE::PlayerCharacter* a_player)
		{
			const auto actor = g_scan.actor.get();
			if (!Usable(actor.get(), a_player)) {
				return Finish();
			}
			using Flag = RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS;
			int budget = INFOS_PER_STEP;
			while (budget > 0 && g_scan.found < LINES_PER_ACTOR && g_scan.topic < g_topics.size()) {
				auto* topic = g_topics[g_scan.topic];
				const auto next = [&] {
					++g_scan.topic;
					g_scan.info = 0;
				};
				if (!topic->topicInfos || g_scan.info >= topic->numTopicInfos) {
					next();
					continue;
				}
				if (g_scan.info == 0 && (!topic->ownerQuest || !topic->ownerQuest->IsRunning())) {
					next();
					continue;
				}
				auto* info = topic->topicInfos[g_scan.info++];
				--budget;
				++g_scan.checked;
				if (!info || info->IsDeleted() || info->IsIgnored()) {
					continue;
				}
				if (info->data.flags.any(Flag::kSayOnce) && info->saidOnce) {
					continue;
				}
				if (!info->objConditions.IsTrue(actor.get(), a_player)) {
					continue;
				}
				++g_scan.found;
				Build(topic, info, actor.get());
				// The first info that passes is the topic's answer. A run of
				// "random" infos is one draw among those that pass, so the rest of
				// the run is looked at too; anything else ends the topic.
				if (!info->data.flags.any(Flag::kRandom) || info->data.flags.any(Flag::kRandomEnd)) {
					next();
				}
			}
			if (g_scan.found >= LINES_PER_ACTOR || g_scan.topic >= g_topics.size()) {
				Finish();
			}
		}

		// Game thread, every STEP_EVERY while the setting is on.
		void Step()
		{
			struct Done
			{
				~Done() { g_stepPending.store(false); }
			} done;

			if (g_resetWanted.exchange(false)) {
				g_seen.clear();
				g_scan = Scan{};
			}
			if (!Config::Get()->dialoguePrepare || !Speaker::IsDialogueEnabled()) {
				g_scan = Scan{};
				return;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* ui = RE::UI::GetSingleton();
			// not while the player is busy: in a menu, in a conversation (its lines
			// come first), in a fight
			if (!player || !ui || !player->Is3DLoaded() || ui->GameIsPaused() ||
				ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || player->IsInCombat()) {
				return;
			}
			// room for one NPC's worth of lines, or wait: a greeting found while the
			// queue is full would be marked as built and never made
			if (!Speaker::CanPrepareDialogue(LINES_PER_ACTOR * 2)) {
				return;
			}
			if (!g_scan.active) {
				const auto now = Clock::now();
				if (now < g_nextLook) {
					return;
				}
				g_nextLook = now + LOOK_EVERY;
				if (!PickActor(player)) {
					return;
				}
			}
			Continue(player);
		}

		void Ticker()
		{
			for (;;) {
				std::this_thread::sleep_for(STEP_EVERY);
				if (!Config::Get()->dialoguePrepare || g_stepPending.exchange(true)) {
					continue;
				}
				if (const auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([] { Step(); });
				} else {
					g_stepPending.store(false);
				}
			}
		}
	}

	void Start()
	{
		if (g_started.exchange(true)) {
			return;
		}
		auto* data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			return;
		}
		// Every greeting topic is kept, also one with no topic infos at this
		// point. In a large load order 776 of 1,391 had some here, about as many
		// as have infos in a master file (775, counted from the plugins), so the
		// game seems to read those of other plugins later. The list is made once,
		// and the check passes over a topic for as long as it has none.
		for (auto* topic : data->GetFormArray<RE::TESTopic>()) {
			if (topic && topic->data.subtype.get() == RE::DIALOGUE_DATA::Subtype::kHello) {
				g_topics.push_back(topic);
			}
		}
		const auto priority = [](const RE::TESTopic* a_topic) {
			return a_topic->ownerQuest ? static_cast<int>(a_topic->ownerQuest->data.priority) : -1;
		};
		std::stable_sort(g_topics.begin(), g_topics.end(),
			[&](const RE::TESTopic* a_left, const RE::TESTopic* a_right) { return priority(a_left) > priority(a_right); });
		logger::info("Greetings: {} greeting topics known, {} of them with topic infos at this point; making greetings "
					 "ahead is {} ([dialogue] prepare_greetings)",
			g_topics.size(), WithInfos(), Config::Get()->dialoguePrepare ? "on" : "off");
		std::thread(Ticker).detach();
	}

	void Reset()
	{
		g_resetWanted.store(true);
	}
}
