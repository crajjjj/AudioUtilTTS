// The in-game menu's config writer and schema, run against the shipped
// AudioUtilTTS.toml. No game, no SKSE:
//   xmake build menu-tests && xmake run menu-tests
// Every check works on a copy in the temp folder; the repo is only read.

#include "menu/ConfigDoc.h"
#include "menu/Model.h"
#include "menu/Schema.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	int g_failed = 0;
	int g_checks = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		++g_checks;
		if (!a_ok) {
			++g_failed;
			std::cout << "FAIL: " << a_what << "\n";
		}
	}

	std::string ReadText(const fs::path& a_file)
	{
		std::ifstream in{ a_file, std::ios::binary };
		return { std::istreambuf_iterator<char>{ in }, std::istreambuf_iterator<char>{} };
	}

	void WriteText(const fs::path& a_file, const std::string& a_text)
	{
		std::ofstream out{ a_file, std::ios::binary | std::ios::trunc };
		out << a_text;
	}

	std::vector<std::string> SplitLines(const std::string& a_text)
	{
		std::vector<std::string> lines;
		std::istringstream       in{ a_text };
		for (std::string line; std::getline(in, line);) {
			lines.push_back(line);
		}
		return lines;
	}

	int DifferingLines(const std::string& a_before, const std::string& a_after)
	{
		const auto before = SplitLines(a_before);
		const auto after = SplitLines(a_after);
		if (before.size() != after.size()) {
			return -1;
		}
		int differ = 0;
		for (std::size_t i = 0; i < before.size(); ++i) {
			differ += before[i] != after[i] ? 1 : 0;
		}
		return differ;
	}

	Core::Row* Row(Core::Model& a_model, std::string_view a_key)
	{
		return a_model.Find(a_key);
	}

	std::size_t IndexOf(Core::Model& a_model, std::string_view a_key)
	{
		for (std::size_t i = 0; i < a_model.Rows().size(); ++i) {
			if (a_model.Rows()[i].key == a_key) {
				return i;
			}
		}
		return static_cast<std::size_t>(-1);
	}

	// A fresh copy of the shipped file, with the defaults beside it.
	struct Sandbox
	{
		fs::path dir;
		fs::path file;
		fs::path defaults;
		std::string original;

		explicit Sandbox(const fs::path& a_shipped, const std::string& a_name)
		{
			dir = fs::temp_directory_path() / "AudioUtilTTS-menu-tests" / a_name;
			fs::remove_all(dir);
			fs::create_directories(dir);
			file = dir / "AudioUtilTTS.toml";
			defaults = dir / "AudioUtilTTS.defaults.toml";
			fs::copy_file(a_shipped, file);
			fs::copy_file(a_shipped, defaults);
			original = ReadText(file);
		}
	};

	// --------------------------------------------------------------- the checks

	void SchemaCoversTheFile(const fs::path& a_shipped)
	{
		Core::ConfigDoc doc;
		Check(doc.Load(a_shipped), "the shipped AudioUtilTTS.toml parses");
		const auto& schema = Core::TtsSchema();
		for (const auto& entry : doc.Entries()) {
			Check(schema.Find(entry.key) != nullptr, "the schema describes " + entry.key);
			Check(schema.PageOf(entry.section) >= 0, "a page shows the table of " + entry.key);
		}
		for (const auto& spec : schema.Settings()) {
			if (spec.key.find('*') == std::string::npos) {
				Check(doc.Find(spec.key) != nullptr, "the schema key " + spec.key + " is in the shipped file");
			}
			for (const char c : spec.label + spec.tip + spec.group) {
				if (static_cast<unsigned char>(c) >= 0x80) {
					Check(false, "the schema text of " + spec.key + " is ASCII");
					break;
				}
			}
			Check(spec.label.find("##") == std::string::npos, "no ## in the label of " + spec.key);
		}
	}

	void UnchangedValuesLeaveTheFileAsItIs(const fs::path& a_shipped)
	{
		Sandbox     box{ a_shipped, "unchanged" };
		Core::Model model;
		model.Load(box.file, box.defaults, Core::TtsSchema());
		std::vector<std::size_t> all(model.Rows().size());
		for (std::size_t i = 0; i < all.size(); ++i) {
			all[i] = i;
		}
		Check(model.Commit(all) && model.Writes() == 0, "the menu writes nothing that was not changed");
		Check(ReadText(box.file) == box.original, "so the file stays byte-identical");

		// Forced through the writer: a number or a switch comes back as it was.
		// A string comes back as the same value, in single quotes: the writer
		// uses toml++'s own way of writing a value, which prefers a literal
		// string, and checks the result against it (AudioUtil's TomlUtil does
		// the same).
		Core::ConfigDoc doc;
		doc.Load(box.file);
		std::vector<Core::Edit> edits;
		for (const auto& entry : doc.Entries()) {
			edits.push_back({ entry.key, entry.value, false });
		}
		std::vector<Core::Outcome> outcomes;
		Core::ConfigDoc            writer;
		Check(writer.Rewrite(box.file, edits, outcomes), "every key re-set to its own value is written");
		Check(std::count(outcomes.begin(), outcomes.end(), Core::Outcome::Written) == static_cast<std::ptrdiff_t>(edits.size()),
			"every key is accepted");
		const auto after = SplitLines(ReadText(box.file));
		const auto before = SplitLines(box.original);
		Check(after.size() == before.size(), "no line is added or lost");
		for (std::size_t i = 0; i < before.size() && i < after.size(); ++i) {
			if (before[i] != after[i]) {
				std::string unquoted = after[i];
				std::replace(unquoted.begin(), unquoted.end(), '\'', '"');
				Check(unquoted == before[i], "only quotes change on line " + std::to_string(i + 1) + ": " + after[i]);
			}
		}
		Core::ConfigDoc reread;
		Check(reread.Load(box.file) && reread.Entries().size() == doc.Entries().size(), "the file reads back with every key");
		for (const auto& entry : doc.Entries()) {
			const auto* again = reread.Find(entry.key);
			Check(again && Core::Identical(again->value, entry.value), entry.key + " reads back as before");
		}
	}

	void OneChangeOneLine(const fs::path& a_shipped)
	{
		Sandbox     box{ a_shipped, "one-change" };
		Core::Model model;
		Check(model.Load(box.file, box.defaults, Core::TtsSchema()), "the model loads");
		auto* row = Row(model, "dialogue.fallback");
		Check(row != nullptr, "a row for dialogue.fallback");
		if (!row) {
			return;
		}
		row->edit = true;
		Check(model.Commit({ IndexOf(model, "dialogue.fallback") }), "the change is written");
		const auto after = ReadText(box.file);
		Check(DifferingLines(box.original, after) == 1, "one change touches one line");
		const auto table = toml::parse(after);
		Check(table.at_path("dialogue.fallback").is_boolean() && table.at_path("dialogue.fallback").value_or(false),
			"a switch stays a TOML boolean");

		auto* threads = Row(model, "model.threads");
		auto* speed = Row(model, "voice.speed");
		Check(threads && speed, "rows for model.threads and voice.speed");
		if (threads && speed) {
			threads->edit = std::int64_t{ 4 };
			speed->edit = Core::FromFloat(1.1f);
			Check(model.Commit({ IndexOf(model, "model.threads"), IndexOf(model, "voice.speed") }), "two changes are written");
			const auto again = toml::parse(ReadText(box.file));
			Check(again.at_path("model.threads").is_integer() && again.at_path("model.threads").value_or(0) == 4, "an int is written as an int");
			Check(again.at_path("voice.speed").is_floating_point() && again.at_path("voice.speed").value_or(0.0) == 1.1, "a float is written as a float, as typed");
		}
	}

	void PresetsAndVoiceTypes(const fs::path& a_shipped)
	{
		Sandbox     box{ a_shipped, "presets" };
		Core::Model model;
		model.Load(box.file, box.defaults, Core::TtsSchema());

		// the page of presets: one group per preset, every key offered
		const Core::Page* presets = nullptr;
		const Core::Page* voiceTypes = nullptr;
		for (const auto& page : model.Pages()) {
			presets = page.title == "Presets" ? &page : presets;
			voiceTypes = page.title == "Voice types" ? &page : voiceTypes;
		}
		Check(presets != nullptr, "a Presets page");
		Check(voiceTypes != nullptr, "a Voice types page, also with no line in the file");
		Check(model.Pages().size() == Core::TtsSchema().Pages().size(), "no Other page for the shipped file");
		for (const auto& page : model.Pages()) {
			const auto advanced = std::find_if(page.groups.begin(), page.groups.end(),
				[](const Core::Group& g) { return g.title == Core::kAdvancedGroup; });
			Check(advanced == page.groups.end() || advanced + 1 == page.groups.end(), page.title + ": Advanced is the last group");
			Check(&page == voiceTypes || advanced != page.groups.begin(), page.title + ": a settings page opens with settings, not with Advanced");
		}
		if (presets) {
			std::vector<std::string> titles;
			for (const auto& group : presets->groups) {
				titles.push_back(group.title);
			}
			Check(titles == std::vector<std::string>{ "Preset narrator", "Preset female", "Preset male", "Preset whisper",
									"Preset bella" },
				"one group per preset, in file order");
			for (const auto& group : presets->groups) {
				std::vector<std::string> names;
				for (const auto index : group.rows) {
					names.push_back(model.Rows()[index].name);
				}
				Check(names == std::vector<std::string>{ "model", "voice", "speed", "volume" }, group.title + " offers model, voice, speed, volume");
			}
		}

		auto* speed = Row(model, "preset.whisper.speed");
		Check(speed && !speed->inFile && speed->inherit == "voice.speed", "a preset's speed is offered while the file leaves it out");
		if (speed) {
			Check(Core::SameValue(speed->saved, 1.0), "it shows the default voice's speed");
			speed->edit = Core::FromFloat(1.25f);
			Check(model.Commit({ IndexOf(model, "preset.whisper.speed") }), "setting it is written");
			const auto after = ReadText(box.file);
			const auto table = toml::parse(after);
			Check(table.at_path("preset.whisper.speed").value_or(0.0) == 1.25, "the speed lands in [preset.whisper]");
			Check(table.at_path("preset.whisper.voice").value_or(-1) == 2, "the preset keeps its voice");
			Check(SplitLines(after).size() == SplitLines(box.original).size() + 1, "one line is added");
			auto* again = Row(model, "preset.whisper.speed");
			Check(again && again->inFile, "the row is in the file now");
		}

		const auto before = ReadText(box.file);
		Check(model.Add({ { "voicetype.MaleNord", Core::Scalar{ std::string{ "male" } }, true } }), "a voice type line is added");
		const auto after = toml::parse(ReadText(box.file));
		Check(after.at_path("voicetype.MaleNord").value_or(std::string{}) == "male", "the line lands in [voicetype]");
		Check(SplitLines(ReadText(box.file)).size() == SplitLines(before).size() + 1, "adding touches one line");
		Check(Row(model, "voicetype.MaleNord") != nullptr, "the new line gets a row");
		Check(model.Add({ { "voicetype.MaleNord", Core::Scalar{ std::string{ "female" } }, true } }) &&
				  toml::parse(ReadText(box.file)).at_path("voicetype.MaleNord").value_or(std::string{}) == "male",
			"adding a line that exists leaves it alone");
		const auto* row = Row(model, "voicetype.MaleNord");
		Check(row && row->label == "MaleNord", "a voice type line is labelled with the ID as written");
	}

	void HandEditsSurvive(const fs::path& a_shipped)
	{
		Sandbox     box{ a_shipped, "hand-edit" };
		Core::Model model;
		model.Load(box.file, box.defaults, Core::TtsSchema());
		// someone changes the file behind the menu's back
		auto text = box.original;
		const auto at = text.find("max_loaded = 4");
		Check(at != std::string::npos, "the shipped file says max_loaded = 4");
		if (at == std::string::npos) {
			return;
		}
		text.replace(at, 14, "max_loaded = 6");
		WriteText(box.file, text);
		auto* volume = Row(model, "voice.volume");
		volume->edit = Core::FromFloat(0.5f);
		Check(model.Commit({ IndexOf(model, "voice.volume") }), "a change is written over a hand edit");
		const auto table = toml::parse(ReadText(box.file));
		Check(table.at_path("model.max_loaded").value_or(0) == 6, "the hand edit survives the menu's write");
		Check(table.at_path("voice.volume").value_or(0.0) == 0.5, "and the menu's change is there");
		auto* maxLoaded = Row(model, "model.max_loaded");
		Check(maxLoaded && Core::SameValue(maxLoaded->edit, std::int64_t{ 6 }), "the menu shows the hand edit afterwards");
	}

	void BrokenFileIsNotWritten(const fs::path& a_shipped)
	{
		Sandbox box{ a_shipped, "broken" };
		WriteText(box.file, box.original + "\n[[[ not toml\n");
		const auto  broken = ReadText(box.file);
		Core::Model model;
		Check(!model.Load(box.file, box.defaults, Core::TtsSchema()), "a broken file is not used");
		Check(model.Pages().size() == Core::TtsSchema().Pages().size(), "the pages still exist, to show why");
		Check(model.Rows().empty(), "and they hold no rows");
		Check(ReadText(box.file) == broken, "and the file is left alone");
	}

	// Every ImGui wrapper Menu.cpp calls must have its export on Env.h's probe
	// list: the wrappers do not check for a missing export.
	void ProbeListCoversTheMenu()
	{
		const auto header = ReadText("vendor/SKSEMenuFramework/SKSEMenuFramework.h");
		const auto menu = ReadText("src/menu/Menu.cpp");
		const auto env = ReadText("src/menu/Env.h");
		Check(!header.empty() && !menu.empty() && !env.empty(), "the menu sources are found (run from the repo root)");

		std::map<std::string, std::set<std::string>> exportsOf;  // wrapper name -> exports its overloads use
		{
			const std::regex fn{ R"(^\s*inline\s+[^(]*?\b(\w+)\s*\()" };
			const std::regex ex{ R"re(GetMenuFrameworkFunction<\w+>\("(\w+)"\))re" };
			std::string      current;
			for (const auto& line : SplitLines(header)) {
				std::smatch m;
				if (std::regex_search(line, m, fn)) {
					current = m[1];
				}
				if (std::regex_search(line, m, ex) && !current.empty()) {
					exportsOf[current].insert(m[1]);
				}
			}
		}
		std::set<std::string> listed;
		{
			const std::regex quoted{ R"re("(\w+)")re" };
			const auto       begin = env.find("kFrameworkExports");
			const auto       end = env.find("};", begin);
			const auto       list = env.substr(begin, end - begin);
			for (std::sregex_iterator it{ list.begin(), list.end(), quoted }, last; it != last; ++it) {
				listed.insert((*it)[1]);
			}
		}
		const std::regex call{ R"(\bUi::(\w+)\s*\()" };
		std::set<std::string> used;
		for (std::sregex_iterator it{ menu.begin(), menu.end(), call }, last; it != last; ++it) {
			used.insert((*it)[1]);
		}
		for (const auto& name : used) {
			const auto it = exportsOf.find(name);
			if (it == exportsOf.end()) {
				continue;  // a type or a constant, not a function
			}
			bool covered = false;
			for (const auto& ex : it->second) {
				covered = covered || listed.contains(ex);
			}
			Check(covered, "Env.h probes the export of Ui::" + name);
		}
		for (const auto& name : { "AddSectionItem", "RegisterEventPriority", "UnregisterEvent" }) {
			Check(listed.contains(name), std::string{ "Env.h probes " } + name);
		}
	}
}

int main()
{
	const fs::path shipped = "dist/SKSE/Plugins/AudioUtilTTS/AudioUtilTTS.toml";
	if (!fs::exists(shipped)) {
		std::cout << "run from the repo root: " << shipped.string() << " not found\n";
		return 2;
	}
	try {
		SchemaCoversTheFile(shipped);
		UnchangedValuesLeaveTheFileAsItIs(shipped);
		OneChangeOneLine(shipped);
		PresetsAndVoiceTypes(shipped);
		HandEditsSurvive(shipped);
		BrokenFileIsNotWritten(shipped);
		ProbeListCoversTheMenu();
	} catch (const std::exception& e) {
		std::cout << "FAIL: an exception: " << e.what() << "\n";
		++g_failed;
	}
	std::cout << g_checks << " checks, " << g_failed << " failed\n";
	return g_failed == 0 ? 0 : 1;
}
