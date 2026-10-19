#include "menu/Menu.h"

#include "Config.h"
#include "Speaker.h"
#include "menu/Env.h"
#include "menu/Model.h"
#include "menu/Ui.h"

#include <cmath>
#include <functional>
#include <limits>
#include <thread>

// Everything below RenderSettings / RenderStatus / OnEvent runs inside a callback
// SKSE Menu Framework invoked, on whichever thread it renders from. So it touches
// the model under g_lock, the config file, and ImGui through Ui.h - and nothing
// of the game: what needs the game (speaking a line, the console's selected NPC,
// applying a config) goes onto the SKSE task queue, and what reads the cache
// folder (the status text, clearing the cache) runs on a thread of its own.
// Adapted from SLO VE's settings menu (skse\src\Menu.cpp).

namespace Menu
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr std::size_t kMaxSettingsPages = 8;
		constexpr std::size_t kNone = static_cast<std::size_t>(-1);
		constexpr auto        kReopenGap = 1s;     // no frame drawn for this long: the menu was closed
		constexpr auto        kRetryFailed = 2s;   // a value the file refused is tried again this often
		constexpr auto        kStatusEvery = 2s;   // the status text is made again this often while shown
		constexpr auto        kApplyRetry = 1s;    // a config the plugin could not read back is tried again
		constexpr int         kApplyRetries = 3;
		constexpr std::size_t kTextCapacity = 4096;  // bytes a text field can hold, terminator included
		constexpr std::size_t kLineCapacity = 512;   // the line a voice is tried with
		constexpr std::size_t kIdCapacity = 128;     // a voice type ID
		constexpr const char* kSender = "Menu";      // the sender name the log shows for a line tried here
		constexpr const char* kTryLine = "The road to Whiterun lay open.";

		const auto kConfigDir = std::filesystem::path{ "Data" } / "SKSE" / "Plugins" / "AudioUtilTTS";
		const auto kConfigFile = kConfigDir / "AudioUtilTTS.toml";
		const auto kDefaultsFile = kConfigDir / "AudioUtilTTS.defaults.toml";  // a copy of the shipped file, made by the build

		struct RowUi
		{
			std::vector<char> text;     // InputText edits in place; sized on first use, text rows only
			std::string       textFor;  // the value the buffer was last filled from
			bool              textInit = false;
			bool              wasActive = false;
			std::uint64_t     drawn = 0;     // the g_frame this row was last drawn in
			Clock::time_point failedAt{};    // when the file last refused this row's value
		};

		// ------------------------------------------------------------- g_lock
		std::mutex               g_lock;
		Core::Model              g_model;
		std::vector<RowUi>       g_ui;  // parallel to g_model.Rows()
		std::size_t              g_settingsPages = 0;  // settings pages registered, in schema order
		std::size_t              g_voiceTypesPage = kNone;  // the page that gets the "add a line" form
		Clock::time_point        g_lastFrame{};
		std::uint64_t            g_frame = 0;  // counts page draws (and menu closes)
		std::vector<std::size_t> g_commits;    // rows to write once this frame's ImGui calls are done
		std::vector<Core::Edit>  g_adds;       // voice type lines to add, the same way
		std::string              g_writeError;
		std::size_t              g_confirmReset = kNone;  // the page whose "reset" awaits a yes
		bool                     g_confirmClear = false;  // "clear the cache" awaits a yes
		bool                     g_reloadAsked = false;
		bool                     g_loggedFault = false;
		std::vector<char>        g_tryLine;     // the Status page's line
		std::string              g_tryPreset;   // "" = the default voice
		std::vector<char>        g_newId;       // the Voice types page's form
		std::string              g_newPreset;
		std::string              g_addNote;

		// ------------------------------------- written by tasks and threads
		std::mutex        g_resultLock;
		Speaker::Summary  g_summary;       // what the Status page shows
		bool              g_haveSummary = false;
		std::string       g_details;       // Speaker::Status() and DialogueStatus(), while "Details" is open
		Clock::time_point g_statusAt{};
		std::atomic<bool> g_wantDetails{ false };
		std::string       g_actionNote;    // what the last button did
		std::string       g_voiceNote;     // which voice the selected NPC gets
		std::atomic<bool> g_statusBusy{ false };
		std::atomic<bool> g_applyQueued{ false };
		std::atomic<int>  g_applyResult{ -1 };  // the last Speaker::ApplyResult, -1 = none yet
		std::atomic<int>  g_applyRetries{ 0 };
		std::atomic<std::int64_t> g_applyAt{ 0 };  // when the last apply ran, steady clock ticks

		const Ui::ImVec4 kWarn{ 1.0f, 0.5f, 0.4f, 1.0f };
		const Ui::ImVec4 kHint{ 0.7f, 0.7f, 0.7f, 1.0f };
		const Ui::ImVec4 kGood{ 0.55f, 0.85f, 0.55f, 1.0f };

		// ------------------------------------------------------------ helpers

		void Note(std::string a_text)
		{
			std::scoped_lock lock{ g_resultLock };
			g_actionNote = std::move(a_text);
		}

		std::string Read(const std::string& a_field)
		{
			std::scoped_lock lock{ g_resultLock };
			return a_field;
		}

		// Onto the game thread. Every call into the game, and into the plugin's
		// speaking side, goes this way.
		void Post(std::function<void()> a_task)
		{
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) {
				return;
			}
			tasks->AddTask([task = std::move(a_task)] {
				try {
					task();
				} catch (const std::exception& e) {
					logger::error("Menu: a task failed: {}", e.what());
				} catch (...) {
					logger::error("Menu: a task failed");
				}
			});
		}

		// The file changed: the plugin reads it again. One request on its way
		// covers every write made before it runs, since it reads the file then.
		void RequestApply()
		{
			if (g_applyQueued.exchange(true)) {
				return;
			}
			Post([] {
				g_applyQueued.store(false);  // first: a write after this point asks again
				const auto result = Speaker::ApplyConfig();
				g_applyResult.store(static_cast<int>(result));
				g_applyAt.store(Clock::now().time_since_epoch().count());
				if (result == Speaker::ApplyResult::kNotParsed) {
					logger::warn("Menu: AudioUtilTTS.toml could not be read back after a change, the previous settings are kept");
				} else {
					g_applyRetries.store(0);
					logger::info("Menu: settings applied{}", result == Speaker::ApplyResult::kReloaded ? ", the models are loading again" : "");
				}
			});
			if (!SKSE::GetTaskInterface()) {
				g_applyQueued.store(false);
			}
		}

		// The status text, made off the render thread: the dialogue part counts
		// the files of the cache folder.
		void RefreshStatus()
		{
			{
				std::scoped_lock lock{ g_resultLock };
				if (g_statusAt != Clock::time_point{} && Clock::now() - g_statusAt < kStatusEvery) {
					return;
				}
			}
			if (g_statusBusy.exchange(true)) {
				return;
			}
			std::thread([] {
				try {
					auto summary = Speaker::GetSummary();
					auto details = g_wantDetails.load() ? Speaker::Status() + " | " + Speaker::DialogueStatus() : std::string{};
					std::scoped_lock lock{ g_resultLock };
					g_summary = std::move(summary);
					g_haveSummary = true;
					g_details = std::move(details);
					g_statusAt = Clock::now();
				} catch (...) {
				}
				g_statusBusy.store(false);
			}).detach();
		}

		// Game thread. The actor the console has selected, else the player.
		std::pair<RE::Actor*, std::string> Target()
		{
			RE::Actor* actor = nullptr;
			if (const auto ref = RE::Console::GetSelectedRef()) {
				actor = ref->As<RE::Actor>();
			}
			const bool selected = actor != nullptr;
			if (!actor) {
				actor = RE::PlayerCharacter::GetSingleton();
			}
			std::string who;
			if (actor) {
				const char* name = actor->GetDisplayFullName();  // can be null
				who = name && *name ? name : (selected ? "The selected NPC" : "The player");
			}
			return { actor, who };
		}

		std::string NotTaken()
		{
			return "Not taken: another line is still on its way, or this voice has no model. AudioUtilTTS.log says which.";
		}

		void Colored(const Ui::ImVec4& a_color, const std::string& a_text)
		{
			Ui::PushStyleColor(Ui::ImGuiCol_Text, a_color);
			Ui::TextUnformatted(a_text.c_str());
			Ui::PopStyleColor();
		}

		void Wrapped(const std::string& a_text, const Ui::ImVec4* a_color = nullptr)
		{
			Ui::PushTextWrapPos(0.0f);
			if (a_color) {
				Colored(*a_color, a_text);
			} else {
				Ui::TextUnformatted(a_text.c_str());
			}
			Ui::PopTextWrapPos();
		}

		// "a | b | c" as one line per part
		void Lines(const std::string& a_text)
		{
			std::size_t start = 0;
			while (start <= a_text.size()) {
				const auto bar = a_text.find(" | ", start);
				const auto part = a_text.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
				if (!part.empty()) {
					Wrapped("- " + part);
				}
				if (bar == std::string::npos) {
					break;
				}
				start = bar + 3;
			}
		}

		void FillBuffer(std::vector<char>& a_buffer, std::size_t a_capacity, std::string_view a_text)
		{
			a_buffer.assign(a_capacity, '\0');
			std::copy_n(a_text.begin(), std::min(a_text.size(), a_capacity - 1), a_buffer.begin());
		}

		// the presets the plugin knows, names as it compares them (lower case),
		// sorted; presets from mods' files included
		std::vector<std::string> PresetNames()
		{
			std::vector<std::string> names;
			const auto               settings = Config::Get();
			for (const auto& [name, preset] : settings->presets) {
				names.push_back(name);
			}
			std::sort(names.begin(), names.end());
			return names;
		}

		// A combo over "" (shown as a_none) and every preset. True when changed.
		bool PresetCombo(const char* a_label, std::string& a_value, const char* a_none)
		{
			bool        changed = false;
			const auto  preview = a_value.empty() ? std::string{ a_none } : a_value;
			if (Ui::BeginCombo(a_label, preview.c_str())) {
				if (Ui::Selectable(a_none, a_value.empty())) {
					a_value.clear();
					changed = true;
				}
				for (const auto& name : PresetNames()) {
					if (Ui::Selectable(name.c_str(), name == a_value)) {
						a_value = name;
						changed = true;
					}
				}
				Ui::EndCombo();
			}
			return changed;
		}

		bool IsBareKey(std::string_view a_key)
		{
			return !a_key.empty() && std::all_of(a_key.begin(), a_key.end(), [](unsigned char c) {
				return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
			});
		}

		// "Label   value", the values lined up
		void Fact(const char* a_label, const std::string& a_value, const Ui::ImVec4* a_color = nullptr)
		{
			Colored(kHint, a_label);
			Ui::SameLine(Ui::GetFontSize() * 7.0f);
			Wrapped(a_value, a_color);
		}

		void DrawSummary(const Speaker::Summary& a_summary)
		{
			if (!a_summary.notReady.empty()) {
				Fact("Speech", "not ready: " + a_summary.notReady, &kWarn);
			} else if (a_summary.switchedOff) {
				Fact("Speech", "switched off for this session (autts enable 1)", &kWarn);
			} else {
				Fact("Speech", "ready", &kGood);
			}
			Fact("Default voice", a_summary.defaultModel.empty() ?
									  "none (" + a_summary.noDefaultWhy + "): narration and the generic voices are silent" :
									  std::format("{}, {} voices", a_summary.defaultModel, a_summary.defaultVoices));
			Fact("Voice models", std::format("{} loaded, {} installed", a_summary.modelsLoaded, a_summary.modelsInstalled));
			if (a_summary.dialogue == "on") {
				Fact("Dialogue", std::format("on: {} lines given a voice, {} left unvoiced this session", a_summary.given, a_summary.unvoiced));
			} else {
				Fact("Dialogue", a_summary.dialogue, &kWarn);
			}
			Fact("Your lines", a_summary.player, a_summary.player.starts_with("on: ") ? nullptr : &kWarn);
			Fact("Line cache", std::format("{} lines, {:.0f} MB", a_summary.cacheLines, a_summary.cacheMb));
		}

		// ---------------------------------------------------------- the model

		void SyncUi()
		{
			g_ui.assign(g_model.Rows().size(), RowUi{});
			g_commits.clear();
			g_confirmReset = kNone;
		}

		// The menu was closed and is open again: show what is on disk now. A hand
		// edit made in between is applied too, as `autts reload` would.
		void Reopened()
		{
			const bool changed = g_model.Refresh();
			SyncUi();
			g_writeError.clear();
			if (changed && g_model.Ok()) {
				logger::info("Menu: AudioUtilTTS.toml changed on disk since it was last read, applying it");
				RequestApply();
			}
		}

		// Changed, and worth a write now. A value the file refused is tried again
		// after a pause, not every frame.
		bool Writable(std::size_t a_index)
		{
			const auto& row = g_model.Rows()[a_index];
			if (!row.Dirty()) {
				return false;
			}
			if (row.failed && Core::Identical(*row.failed, row.edit)) {
				return Clock::now() - g_ui[a_index].failedAt >= kRetryFailed;
			}
			return true;
		}

		// Queue every changed row that was NOT drawn this frame: its page was
		// switched away from, its group collapsed or the menu closed, so no later
		// frame is promised to it. a_lastChance: the rows are about to be read
		// from disk again, so a refused value is tried once more without waiting.
		void QueueLeftBehind(bool a_lastChance = false)
		{
			for (std::size_t i = 0; i < g_model.Rows().size() && i < g_ui.size(); ++i) {
				if (g_ui[i].drawn != g_frame && (a_lastChance ? g_model.Rows()[i].Dirty() : Writable(i))) {
					g_commits.push_back(i);
				}
			}
		}

		const char* AppliesText(Core::Applies a_applies)
		{
			switch (a_applies) {
			case Core::Applies::Reload:
				return "A change loads the voice models again, which takes a second or two.";
			case Core::Applies::Restart:
				return "Takes effect after the next game start.";
			default:
				return "";
			}
		}

		void DrawTooltip(const Core::Row& a_row)
		{
			if (!Ui::BeginTooltip()) {
				return;
			}
			Ui::PushTextWrapPos(Ui::GetFontSize() * 30.0f);
			if (!a_row.tip.empty()) {
				Ui::TextUnformatted(a_row.tip.c_str());
			}
			if (a_row.shipped) {
				const std::string line = "Default: " + Core::Display(*a_row.shipped);
				Ui::TextUnformatted(line.c_str());
			}
			if (const char* applies = AppliesText(a_row.applies); *applies) {
				Colored(kHint, applies);
			}
			if (!a_row.inFile) {
				Colored(kHint, a_row.inherit.empty() ?
								   "Not in your AudioUtilTTS.toml: the plugin uses the value shown. A change adds the line." :
								   "Not set here: this uses the value shown, the default voice's. A change sets it here.");
			}
			if (a_row.failed) {
				Colored(kWarn, "The last change to this setting could not be written.");
			}
			Colored(kHint, a_row.key);
			Ui::PopTextWrapPos();
			Ui::EndTooltip();
		}

		int ToInt(const Core::Scalar& a_value)
		{
			const auto* i = std::get_if<std::int64_t>(&a_value);
			return i ? static_cast<int>(std::clamp<std::int64_t>(*i, std::numeric_limits<int>::min(), std::numeric_limits<int>::max())) : 0;
		}

		void DrawRow(std::size_t a_index)
		{
			auto& row = g_model.Rows()[a_index];
			auto& ui = g_ui[a_index];
			ui.drawn = g_frame;

			// An int key someone wrote as a float (60.0) is shown as the whole
			// number it is meant to be; row.edit only changes when the widget
			// does, so nothing is written that was not touched.
			const bool   intLike = row.control == Core::Control::Int || row.control == Core::Control::Percent;
			Core::Scalar shown = row.edit;
			if (intLike) {
				if (const auto* f = std::get_if<double>(&row.edit)) {
					shown = std::isfinite(*f) ? static_cast<std::int64_t>(std::llround(std::clamp(*f, -2.0e9, 2.0e9))) : std::int64_t{ 0 };
				}
			}
			const auto kind = Core::KindOf(shown);

			std::string label = row.label;
			if (!row.unit.empty() && row.control != Core::Control::Percent) {
				label += " (" + row.unit + ")";
			}

			Ui::PushID(static_cast<int>(a_index));
			Ui::SetNextItemWidth(Ui::GetFontSize() * 11.0f);

			if (kind == Core::Kind::Bool) {
				bool value = std::get<bool>(row.edit);
				if (Ui::Checkbox(label.c_str(), &value)) {
					row.edit = value;
				}
			} else if (kind == Core::Kind::Int) {
				int  value = ToInt(shown);
				bool changed = false;
				if (row.control == Core::Control::Percent) {
					changed = Ui::SliderInt(label.c_str(), &value, 0, 100, "%d%%", Ui::ImGuiSliderFlags_AlwaysClamp);
				} else if (row.min && row.max && !row.step) {
					changed = Ui::SliderInt(label.c_str(), &value, static_cast<int>(*row.min), static_cast<int>(*row.max), "%d", Ui::ImGuiSliderFlags_AlwaysClamp);
				} else {
					const int step = row.step ? std::max(1, static_cast<int>(*row.step)) : 1;
					changed = Ui::InputInt(label.c_str(), &value, step, step * 10);
				}
				if (changed) {
					if (row.min) {
						value = std::max(value, static_cast<int>(*row.min));
					}
					if (row.max) {
						value = std::min(value, static_cast<int>(*row.max));
					}
					row.edit = static_cast<std::int64_t>(value);
				}
			} else if (kind == Core::Kind::Float) {
				float value = static_cast<float>(std::get<double>(row.edit));
				bool  changed = false;
				if (row.min && row.max && !row.step) {
					changed = Ui::SliderFloat(label.c_str(), &value, static_cast<float>(*row.min), static_cast<float>(*row.max), "%.2f", Ui::ImGuiSliderFlags_AlwaysClamp);
				} else {
					const float step = row.step ? static_cast<float>(*row.step) : 0.1f;
					changed = Ui::InputFloat(label.c_str(), &value, step, step * 10.0f, "%.2f");
				}
				if (changed) {
					if (row.min) {
						value = std::max(value, static_cast<float>(*row.min));
					}
					if (row.max) {
						value = std::min(value, static_cast<float>(*row.max));
					}
					row.edit = Core::FromFloat(value);  // only on a real edit: an untouched value is never re-rounded
				}
			} else if (std::get<std::string>(row.edit).size() >= kTextCapacity) {
				// longer than the field can hold: editing it here would cut it short
				Ui::TextUnformatted(label.c_str());
				Ui::SameLine();
				Colored(kHint, "(too long to edit here: change it in AudioUtilTTS.toml)");
			} else {
				const auto& current = std::get<std::string>(row.edit);
				if (ui.text.size() != kTextCapacity) {
					ui.text.assign(kTextCapacity, '\0');
					ui.textInit = false;
				}
				if (!ui.textInit || (!ui.wasActive && ui.textFor != current)) {
					FillBuffer(ui.text, kTextCapacity, current);  // fits: checked above
					ui.textFor = current;
					ui.textInit = true;
				}
				Ui::SetNextItemWidth(Ui::GetFontSize() * 16.0f);
				if (Ui::InputText(label.c_str(), ui.text.data(), ui.text.size())) {
					row.edit = std::string{ ui.text.data() };
					ui.textFor = std::get<std::string>(row.edit);
				}
			}

			const bool active = Ui::IsItemActive();
			const bool hovered = Ui::IsItemHovered(Ui::ImGuiHoveredFlags_ForTooltip);
			ui.wasActive = active;
			if (hovered) {
				DrawTooltip(row);
			}
			if (row.applies == Core::Applies::Restart) {
				Ui::SameLine();
				Colored(kHint, "(after a restart)");
			}
			if (row.shipped && !Core::SameValue(row.edit, *row.shipped)) {
				Ui::SameLine();
				if (Ui::SmallButton("reset")) {
					g_model.ResetToDefault({ a_index });
				}
			}
			Ui::PopID();

			// written once the widget is let go of: a slider on release, a typed
			// number or text on leaving the field, a checkbox or step button at once
			if (!active && Writable(a_index)) {
				g_commits.push_back(a_index);
			}
		}

		std::vector<std::size_t> RowsOf(const std::vector<std::size_t>& a_pages)
		{
			std::vector<std::size_t> rows;
			for (const auto page : a_pages) {
				for (const auto& group : g_model.Pages()[page].groups) {
					rows.insert(rows.end(), group.rows.begin(), group.rows.end());
				}
			}
			return rows;
		}

		void DrawPage(std::size_t a_page)
		{
			const auto& page = g_model.Pages()[a_page];
			Ui::PushID(static_cast<int>(1000 + a_page));
			if (!page.note.empty()) {
				Wrapped(page.note, &kHint);
			}
			for (const auto& group : page.groups) {
				const auto open = group.title == Core::kAdvancedGroup ? Ui::ImGuiTreeNodeFlags_None : Ui::ImGuiTreeNodeFlags_DefaultOpen;
				if (Ui::CollapsingHeader(group.title.c_str(), open)) {
					for (const auto index : group.rows) {
						DrawRow(index);
					}
					Ui::Spacing();
				}
			}
			Ui::PopID();
		}

		// Only when something is wrong, or about to change: a page is for settings.
		void DrawBanner()
		{
			if (!g_writeError.empty()) {
				Wrapped(g_writeError, &kWarn);
			}
			const auto result = g_applyResult.load();
			if (result == static_cast<int>(Speaker::ApplyResult::kNotParsed)) {
				Wrapped("The plugin could not read AudioUtilTTS.toml back after the last change, so it still runs on the settings from before. Press Reload settings and voices on the Status page.", &kWarn);
			} else if (result == static_cast<int>(Speaker::ApplyResult::kReloaded)) {
				const auto at = Clock::time_point{ Clock::duration{ g_applyAt.load() } };
				if (Clock::now() - at < 6s) {
					Wrapped("The voice models are loading again for the last change.", &kHint);
				}
			}
			if (g_model.Ok() && !g_model.HasDefaults()) {
				Wrapped("AudioUtilTTS.defaults.toml is missing, so default values and reset are not available.", &kHint);
			}
		}

		void DrawFooter(std::size_t a_thunk, const std::vector<std::size_t>& a_pages)
		{
			Ui::Separator();
			Ui::PushID(static_cast<int>(5000 + a_thunk));
			const auto  rows = RowsOf(a_pages);
			std::size_t changed = 0;
			for (const auto index : rows) {
				const auto& row = g_model.Rows()[index];
				changed += row.shipped && !Core::SameValue(row.edit, *row.shipped) ? 1 : 0;
			}
			if (g_confirmReset == a_thunk) {
				Colored(kWarn, "Reset " + std::to_string(changed) + " setting(s) on this page to their defaults?");
				Ui::SameLine();
				if (Ui::SmallButton("Yes, reset")) {
					g_model.ResetToDefault(rows);
					g_confirmReset = kNone;
				}
				Ui::SameLine();
				if (Ui::SmallButton("Cancel")) {
					g_confirmReset = kNone;
				}
			} else if (changed > 0) {
				if (Ui::Button("Reset this page to defaults")) {
					g_confirmReset = a_thunk;
				}
				Ui::SameLine();
			}
			if (Ui::Button("Reload from disk")) {
				g_reloadAsked = true;
			}
			Ui::PopID();
		}

		// The Voice types page: a new line for a voice type the file has none for.
		void DrawAddVoiceType()
		{
			Ui::Spacing();
			if (!Ui::CollapsingHeader("Add a line", Ui::ImGuiTreeNodeFlags_DefaultOpen)) {
				return;
			}
			if (g_newId.size() != kIdCapacity) {
				FillBuffer(g_newId, kIdCapacity, "");
			}
			Ui::SetNextItemWidth(Ui::GetFontSize() * 16.0f);
			Ui::InputText("Voice type ID", g_newId.data(), g_newId.size());
			Ui::SetNextItemWidth(Ui::GetFontSize() * 16.0f);
			PresetCombo("Preset##newvoicetype", g_newPreset, "(never spoken for)");
			if (Ui::Button("Add the line")) {
				const std::string id{ g_newId.data() };
				if (!IsBareKey(id)) {
					g_addNote = "A voice type ID is one word of letters, digits, _ or -, such as MaleNord.";
				} else if (g_model.Doc().Find("voicetype." + id)) {
					g_addNote = id + " has a line already: change it above.";
				} else {
					g_adds.push_back({ "voicetype." + id, Core::Scalar{ g_newPreset }, true });
					g_addNote.clear();
					FillBuffer(g_newId, kIdCapacity, "");
				}
			}
			if (!g_addNote.empty()) {
				Wrapped(g_addNote, &kWarn);
			}
			Wrapped("The ID is the voice type's editor ID. Which voice does the selected NPC get? on the Status page shows an NPC's.", &kHint);
		}

		// All file I/O of the frame, before its first or after its last ImGui
		// call. False when a change could not be written.
		bool Flush()
		{
			const auto before = g_model.Writes();
			bool       ok = true;
			if (!g_commits.empty()) {
				ok = g_model.Commit(g_commits);
				if (!ok) {
					// remember when, per row: Writable tries them again after a pause
					const auto now = Clock::now();
					for (const auto index : g_commits) {
						if (index < g_model.Rows().size() && index < g_ui.size() && g_model.Rows()[index].failed) {
							g_ui[index].failedAt = now;
						}
					}
				}
				g_commits.clear();
			}
			// after the commits: Add builds the rows again, and the indices with them
			if (!g_adds.empty()) {
				if (!g_model.Add(g_adds)) {
					g_addNote = "The line could not be written to AudioUtilTTS.toml.";
				}
				g_adds.clear();
				SyncUi();
			}
			if (g_model.Writes() != before) {
				RequestApply();
			}
			if (!ok) {
				if (g_writeError.empty()) {
					logger::warn("Menu: a change could not be written to AudioUtilTTS.toml");
				}
				g_writeError = "A change could not be written to AudioUtilTTS.toml (is the file read-only, in use by another program, or broken by a hand edit?). It is NOT saved yet; the menu keeps trying while it is open.";
			} else if (g_model.Writes() != before) {
				g_writeError.clear();
			}
			if (g_reloadAsked) {
				g_reloadAsked = false;
				Reopened();
			}
			return ok;
		}

		// A frame of any page. True when the menu was closed since the last one.
		void BeginFrame()
		{
			const auto now = Clock::now();
			++g_frame;
			if (now - g_lastFrame > kReopenGap) {
				// an edit from before the gap goes to the file first: the re-read
				// below would otherwise replace it with what is on disk
				bool lost = false;
				if (g_model.Ok()) {
					QueueLeftBehind(true);
					lost = !Flush();
				}
				Reopened();
				if (lost) {
					g_writeError = "A change made before the menu was last closed could not be written to AudioUtilTTS.toml (is the file read-only?) and was dropped.";
				}
			}
			g_lastFrame = now;

			// the plugin could not read the file back: perhaps it was read while
			// the menu was writing it, so ask again a few times
			if (g_applyResult.load() == static_cast<int>(Speaker::ApplyResult::kNotParsed) && !g_applyQueued.load() &&
				g_applyRetries.load() < kApplyRetries &&
				Clock::now() - Clock::time_point{ Clock::duration{ g_applyAt.load() } } > kApplyRetry) {
				g_applyRetries.fetch_add(1);
				RequestApply();
			}
		}

		void EndFrame()
		{
			if (g_model.Ok()) {
				QueueLeftBehind();
			}
			Flush();
		}

		template <class F>
		void Guarded(F&& a_draw) noexcept
		{
			try {
				std::scoped_lock lock{ g_lock };
				a_draw();
			} catch (const std::exception& e) {
				if (!g_loggedFault) {
					g_loggedFault = true;
					logger::error("Menu: a page failed: {}", e.what());
				}
			} catch (...) {
				if (!g_loggedFault) {
					g_loggedFault = true;
					logger::error("Menu: a page failed");
				}
			}
		}

		// ------------------------------------------------------------- pages

		void RenderSettings(std::size_t a_thunk)
		{
			Guarded([a_thunk] {
				BeginFrame();
				DrawBanner();

				std::vector<std::size_t> pages;
				if (g_model.Ok() && a_thunk < g_model.Pages().size()) {
					pages.push_back(a_thunk);
					// the last page also shows the page of tables no page names
					if (a_thunk + 1 == g_settingsPages) {
						for (std::size_t i = g_settingsPages; i < g_model.Pages().size(); ++i) {
							pages.push_back(i);
						}
					}
				}
				if (!g_model.Ok()) {
					Wrapped("AudioUtilTTS.toml cannot be used: " + g_model.Error(), &kWarn);
					Wrapped("Nothing is written while the file is in this state. Fix it in a text editor, then press Reload from disk.");
				}
				for (const auto page : pages) {
					if (pages.size() > 1) {
						Colored(kHint, g_model.Pages()[page].title);
					}
					DrawPage(page);
				}
				if (g_model.Ok() && a_thunk == g_voiceTypesPage) {
					DrawAddVoiceType();
				}
				DrawFooter(a_thunk, pages);
				EndFrame();
			});
		}

		void RenderStatus()
		{
			Guarded([] {
				BeginFrame();
				RefreshStatus();

				Wrapped("Settings are saved to AudioUtilTTS.toml as you change them, and used from the next line on.", &kHint);
				if (Ui::CollapsingHeader("Status", Ui::ImGuiTreeNodeFlags_DefaultOpen)) {
					Speaker::Summary summary;
					bool             have = false;
					std::string      details;
					{
						std::scoped_lock lock{ g_resultLock };
						summary = g_summary;
						have = g_haveSummary;
						details = g_details;
					}
					if (have) {
						DrawSummary(summary);
					} else {
						Colored(kHint, "Reading...");
					}
					const bool open = Ui::CollapsingHeader("Details", Ui::ImGuiTreeNodeFlags_None);
					if (open && !g_wantDetails.exchange(true)) {
						std::scoped_lock lock{ g_resultLock };
						g_statusAt = {};  // made at once, not at the next round
					}
					if (!open) {
						g_wantDetails.store(false);
					} else if (details.empty()) {
						Colored(kHint, "Reading...");
					} else {
						Lines(details);
					}
					Ui::Spacing();
				}

				if (Ui::CollapsingHeader("Try a voice", Ui::ImGuiTreeNodeFlags_DefaultOpen)) {
					if (g_tryLine.size() != kLineCapacity) {
						FillBuffer(g_tryLine, kLineCapacity, kTryLine);
					}
					Ui::SetNextItemWidth(Ui::GetFontSize() * 22.0f);
					Ui::InputText("Line", g_tryLine.data(), g_tryLine.size());
					Ui::SetNextItemWidth(Ui::GetFontSize() * 16.0f);
					PresetCombo("Voice", g_tryPreset, "(the default voice)");
					const std::string line{ g_tryLine.data() };
					if (Ui::Button("Speak it as narration")) {
						Note("Sending...");
						Post([line, preset = g_tryPreset] {
							Note(Speaker::SpeakAs(line, preset, kSender) ? "Taken: it is spoken in a moment."s : NotTaken());
						});
					}
					Ui::SameLine();
					if (Ui::Button("Speak it as the selected NPC")) {
						Note("Sending...");
						Post([line, preset = g_tryPreset] {
							const auto [actor, who] = Target();
							if (!actor) {
								Note("No one to speak: nothing is selected and the player is not loaded.");
								return;
							}
							Note(Speaker::SpeakActor(actor, line, preset, true, kSender) ? who + " says it in a moment." : NotTaken());
						});
					}
					if (Ui::Button("Which voice does the selected NPC get?")) {
						Post([] {
							const auto [actor, who] = Target();
							auto       text = actor ? who + ": " + Speaker::DescribeVoice(actor) : "No one is selected, and the player is not loaded."s;
							std::scoped_lock lock{ g_resultLock };
							g_voiceNote = std::move(text);
						});
					}
					Wrapped("The selected NPC is the one clicked in the console before opening this menu, else you. With (the default voice), an NPC speaks in their own voice type's voice.", &kHint);
					if (const auto voice = Read(g_voiceNote); !voice.empty()) {
						Wrapped(voice);
					}
					if (const auto note = Read(g_actionNote); !note.empty()) {
						Wrapped(note, &kGood);
					}
					Ui::Spacing();
				}

				if (Ui::CollapsingHeader("Maintenance", Ui::ImGuiTreeNodeFlags_DefaultOpen)) {
					if (Ui::Button("Reload settings and voices")) {
						g_reloadAsked = true;  // the menu shows the file as it is now, too
						Post([] {
							Note(Speaker::Reload() ?
									 "Settings read again; the voices are loading again."s :
									 "AudioUtilTTS.toml does not parse, so the previous settings are kept. AudioUtilTTS.log names the line."s);
						});
					}
					Ui::SameLine();
					Colored(kHint, "(what autts reload does)");
					if (g_confirmClear) {
						Colored(kWarn, "Delete every generated dialogue line? Each is generated again the next time it comes up.");
						if (Ui::SmallButton("Yes, clear it")) {
							g_confirmClear = false;
							Note("Clearing...");
							std::thread([] {
								try {
									const int removed = Speaker::ClearDialogueCache();
									Note(std::format("Dialogue line cache cleared: {} files removed.", removed));
								} catch (...) {
								}
							}).detach();
						}
						Ui::SameLine();
						if (Ui::SmallButton("Cancel")) {
							g_confirmClear = false;
						}
					} else if (Ui::Button("Clear the dialogue line cache")) {
						g_confirmClear = true;
					}
				}
				EndFrame();
			});
		}

		void __stdcall StatusThunk()
		{
			RenderStatus();
		}

		template <std::size_t I>
		void __stdcall SettingsThunk()
		{
			RenderSettings(I);
		}

		template <std::size_t... I>
		constexpr auto MakeThunks(std::index_sequence<I...>)
		{
			return std::array<SKSEMenuFramework::Model::RenderFunction, sizeof...(I)>{ &SettingsThunk<I>... };
		}

		constexpr auto kThunks = MakeThunks(std::make_index_sequence<kMaxSettingsPages>{});

		// The framework's menu opened or closed. Closing with a field still being
		// typed in never draws the frame that would have written it, so write here.
		void __stdcall OnEvent(SKSEMenuFramework::Model::EventType a_event) noexcept
		{
			try {
				if (a_event != SKSEMenuFramework::Model::EventType::kCloseMenu) {
					return;
				}
				std::scoped_lock lock{ g_lock };
				if (!g_model.Ok()) {
					return;
				}
				++g_frame;  // nothing is drawn any more: every changed row is left behind
				QueueLeftBehind();
				Flush();
			} catch (...) {
			}
		}
	}

	void Register()
	{
		Env::Detect();
		const auto& env = Env::Get();
		if (!env.frameworkOk) {
			logger::info("Menu: off, {}. AudioUtilTTS.toml can be edited by hand as before.", env.frameworkWhy);
			return;
		}
		try {
			std::scoped_lock lock{ g_lock };
			const auto&      schema = Core::TtsSchema();
			g_model.Load(kConfigFile, kDefaultsFile, schema);
			SyncUi();
			if (g_model.Ok()) {
				logger::info("Menu: AudioUtilTTS.toml has {} settings on {} page(s); defaults {}",
					g_model.Rows().size(), g_model.Pages().size(), g_model.HasDefaults() ? "loaded" : "missing");
			} else {
				logger::warn("Menu: AudioUtilTTS.toml cannot be used: {}", g_model.Error());
			}

			SKSEMenuFramework::SetSection("AudioUtilTTS");
			SKSEMenuFramework::AddSectionItem("Status", &StatusThunk);
			// the schema's pages, by place: the file can neither add nor remove one
			g_settingsPages = std::min(schema.Pages().size(), kMaxSettingsPages);
			for (std::size_t i = 0; i < g_settingsPages; ++i) {
				const auto& page = schema.Pages()[i];
				if (std::find(page.sections.begin(), page.sections.end(), "voicetype") != page.sections.end()) {
					g_voiceTypesPage = i;
				}
				SKSEMenuFramework::AddSectionItem(page.title, kThunks[i]);
			}
			SKSEMenuFramework::AddEvent(&OnEvent, 0.0f);  // lives as long as the game
			logger::info("Menu: registered, section \"AudioUtilTTS\", {} page(s)", g_settingsPages + 1);
		} catch (const std::exception& e) {
			logger::error("Menu: registration failed: {}", e.what());
		} catch (...) {
			logger::error("Menu: registration failed");
		}
	}
}
