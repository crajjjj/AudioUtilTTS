#include "menu/Model.h"

#include <algorithm>
#include <cctype>
#include <tuple>
#include <unordered_set>

namespace Core
{
	namespace
	{
		// the control a value gets when the schema says nothing, or says something
		// that does not fit the value's type
		Control InferControl(const Scalar& a_value)
		{
			switch (KindOf(a_value)) {
			case Kind::Bool:
				return Control::Flag;
			case Kind::Float:
				return Control::Float;
			case Kind::String:
				return Control::Text;
			default:
				return Control::Int;
			}
		}

		bool Fits(Control a_control, const Scalar& a_value)
		{
			const auto kind = KindOf(a_value);
			switch (a_control) {
			case Control::Flag:
				return kind == Kind::Bool;
			case Control::Int:
			case Control::Percent:
				// an int key someone wrote as 60.0 keeps its control: the menu shows
				// it as the whole number it is meant to be and writes an int back
				return kind == Kind::Int || kind == Kind::Float;
			case Control::Float:
				// a float key someone wrote as an int still edits as a float
				return kind == Kind::Float || kind == Kind::Int;
			case Control::Text:
				return kind == Kind::String;
			default:
				return false;
			}
		}

		// what a float control edits, also when the file says 1 for 1.0
		Scalar AsControl(Control a_control, Scalar a_value)
		{
			if (a_control == Control::Float) {
				if (const auto* i = std::get_if<std::int64_t>(&a_value)) {
					return static_cast<double>(*i);
				}
			}
			return a_value;
		}
	}

	std::string LabelFromName(std::string_view a_name)
	{
		std::string label{ a_name };
		std::replace(label.begin(), label.end(), '_', ' ');
		if (!label.empty()) {
			label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
		}
		return label;
	}

	bool Model::Load(const std::filesystem::path& a_file, const std::filesystem::path& a_defaults, const Schema& a_schema)
	{
		_file = a_file;
		_defaultsFile = a_defaults;
		_schema = &a_schema;
		_writes = 0;
		_defaults.Load(a_defaults);  // optional
		_doc.Load(a_file);
		Build();
		return _doc.Ok();
	}

	bool Model::Refresh()
	{
		const std::string before = _doc.Text();
		_doc.Load(_file);
		Build();
		return _doc.Text() != before;
	}

	void Model::Build()
	{
		_rows.clear();
		_pages.clear();
		if (!_schema) {
			return;
		}
		// every schema page, an empty one too: the menu registers them once, by
		// place, and the file may gain the rows of an empty one later
		for (const auto& spec : _schema->Pages()) {
			_pages.push_back({ spec.title, spec.note, {} });
		}
		if (!_doc.Ok()) {
			return;
		}

		// a row, and what decides its place on its page
		struct Placed
		{
			std::size_t row = 0;
			int         page = -1;
			int         rank = 2;  // 0 a fixed key, 1 a key of a pattern, 2 a key the schema does not know
			std::size_t spec = 0;  // its place in the schema
			std::size_t table = 0;  // its table's place in the file
			std::size_t order = 0;  // the order rows were made in
			std::string group;
		};
		std::vector<Placed>             placed;
		std::unordered_set<std::string> seen;

		// tables in the order the user's file has them: a pattern's rows go table
		// by table ("Preset narrator", then "Preset female")
		const auto& tables = _doc.Sections();
		const auto  tableAt = [&](const std::string& a_section) {
            return static_cast<std::size_t>(std::find(tables.begin(), tables.end(), a_section) - tables.begin());
		};

		const auto addRow = [&](const std::string& a_key, const std::string& a_section, const std::string& a_name,
								const Scalar& a_value, bool a_inFile, const Entry* a_shipped, const std::string& a_inherit) {
			std::string matched;
			std::size_t specAt = 0;
			const auto* spec = _schema->Find(a_key, &matched, &specAt);
			Row         row;
			row.key = a_key;
			row.section = a_section;
			row.name = a_name;
			row.inFile = a_inFile;
			row.inherit = a_inherit;
			if (a_shipped) {
				row.shipped = a_shipped->value;
			}
			if (spec) {
				row.known = true;
				row.label = spec->label.empty() ? LabelFromName(a_name) : Substitute(spec->label, matched);
				row.tip = Substitute(spec->tip, matched);
				row.unit = spec->unit;
				row.applies = spec->applies;
				row.min = spec->min;
				row.max = spec->max;
				row.step = spec->step;
				row.control = spec->control != Control::Auto && Fits(spec->control, a_value) ? spec->control : InferControl(a_value);
			} else {
				row.label = LabelFromName(a_name);
				row.control = InferControl(a_value);
			}
			row.saved = AsControl(row.control, a_value);
			row.edit = row.saved;

			Placed place;
			place.row = _rows.size();
			place.page = _schema->PageOf(a_section);
			place.rank = !spec ? 2 : (matched.empty() ? 0 : 1);
			place.spec = specAt;
			place.table = tableAt(a_section);
			place.order = placed.size();
			place.group = !spec ? std::string{ "Other" } :
			                      Substitute(spec->group.empty() ? std::string_view{ "Settings" } : std::string_view{ spec->group }, matched);
			placed.push_back(std::move(place));
			_rows.push_back(std::move(row));
			seen.insert(a_key);
		};

		// what the user's file holds
		for (const auto& entry : _doc.Entries()) {
			addRow(entry.key, entry.section, entry.name, entry.value, true, _defaults.Find(entry.key), {});
		}
		// what only the shipped file holds: a key of a fixed table that the user's
		// file leaves out runs at this default. Not a key of a pattern (a preset,
		// a voice type line) the user removed: that one is gone for the plugin,
		// and a row for it would bring it back on the first edit.
		for (const auto& entry : _defaults.Entries()) {
			if (seen.contains(entry.key)) {
				continue;
			}
			std::string matched;
			const auto* spec = _schema->Find(entry.key, &matched);
			if (!spec || !matched.empty()) {
				continue;
			}
			addRow(entry.key, entry.section, entry.name, entry.value, false, &entry, {});
		}
		// the keys a pattern offers in each of its tables, where the table leaves
		// them out: shown at what the plugin uses instead
		for (const auto& spec : _schema->Settings()) {
			const auto dot = spec.key.rfind('.');
			if (!spec.everyTable || dot == std::string::npos) {
				continue;
			}
			const auto tablePattern = spec.key.substr(0, dot);
			const auto name = spec.key.substr(dot + 1);
			for (const auto& table : tables) {
				const auto key = table + "." + name;
				if (!Matches(tablePattern, table) || seen.contains(key)) {
					continue;
				}
				std::optional<Scalar> shown;
				if (!spec.inherit.empty()) {
					if (const auto* source = _doc.Find(spec.inherit)) {
						shown = source->value;
					} else if (const auto* shipped = _defaults.Find(spec.inherit)) {
						shown = shipped->value;
					}
				}
				if (!shown) {
					shown = spec.fallback;
				}
				if (shown) {
					addRow(key, table, name, *shown, false, nullptr, spec.inherit);
				}
			}
		}

		// a table no page names gets a page of its own, last, so that the schema's
		// pages keep their places
		std::optional<std::size_t> other;
		for (auto& place : placed) {
			if (place.page < 0) {
				if (!other) {
					other = _pages.size();
					_pages.push_back({ "Other", "Settings this menu does not know, as your file holds them.", {} });
				}
				place.page = static_cast<int>(*other);
			}
		}

		// on a page: the fixed keys as the schema lists them, then a pattern's
		// rows table by table, then what the schema does not know, in file order
		const auto sortKey = [](const Placed& a_place) {
			switch (a_place.rank) {
			case 0:
				return std::make_tuple(0, a_place.spec, std::size_t{ 0 }, a_place.order);
			case 1:
				return std::make_tuple(1, a_place.table, a_place.spec, a_place.order);
			default:
				return std::make_tuple(2, a_place.order, std::size_t{ 0 }, std::size_t{ 0 });
			}
		};
		std::stable_sort(placed.begin(), placed.end(),
			[&](const Placed& a, const Placed& b) { return sortKey(a) < sortKey(b); });
		for (const auto& place : placed) {
			auto&      groups = _pages[static_cast<std::size_t>(place.page)].groups;
			const auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.title == place.group; });
			if (it != groups.end()) {
				it->rows.push_back(place.row);
			} else {
				groups.push_back({ place.group, { place.row } });
			}
		}
		// the closed group of settings few people touch goes last on its page
		for (auto& page : _pages) {
			std::stable_partition(page.groups.begin(), page.groups.end(),
				[](const Group& g) { return g.title != kAdvancedGroup; });
		}
	}

	Row* Model::Find(std::string_view a_key)
	{
		const auto it = std::find_if(_rows.begin(), _rows.end(), [&](const Row& r) { return r.key == a_key; });
		return it != _rows.end() ? &*it : nullptr;
	}

	bool Model::Commit(const std::vector<std::size_t>& a_rows)
	{
		std::vector<Edit>        edits;
		std::vector<std::size_t> touched;
		for (const auto index : a_rows) {
			if (index >= _rows.size() || !_rows[index].Dirty() ||
				std::find(touched.begin(), touched.end(), index) != touched.end()) {
				continue;
			}
			edits.push_back({ _rows[index].key, _rows[index].edit, false });
			touched.push_back(index);
		}
		if (edits.empty()) {
			return true;
		}

		ConfigDoc            fresh;
		std::vector<Outcome> outcomes;
		if (!fresh.Rewrite(_file, edits, outcomes)) {
			for (const auto index : touched) {
				_rows[index].failed = _rows[index].edit;
			}
			return false;
		}
		_doc = std::move(fresh);
		bool wrote = false;
		bool allDone = true;
		for (std::size_t i = 0; i < touched.size(); ++i) {
			auto& row = _rows[touched[i]];
			if (outcomes[i] == Outcome::Written) {
				row.saved = row.edit;
				row.inFile = true;
				row.failed.reset();
				wrote = true;
			} else {
				row.failed = row.edit;
				allDone = false;
			}
		}
		if (wrote) {
			++_writes;
		}
		Sync();
		return allDone;
	}

	bool Model::Add(const std::vector<Edit>& a_edits)
	{
		std::vector<Edit> edits = a_edits;
		for (auto& edit : edits) {
			edit.ifMissing = true;
		}
		ConfigDoc            fresh;
		std::vector<Outcome> outcomes;
		if (edits.empty() || !fresh.Rewrite(_file, edits, outcomes)) {
			return edits.empty();
		}
		_doc = std::move(fresh);
		if (std::find(outcomes.begin(), outcomes.end(), Outcome::Written) != outcomes.end()) {
			++_writes;
		}
		Build();
		return std::find(outcomes.begin(), outcomes.end(), Outcome::Refused) == outcomes.end();
	}

	// _doc is what the disk holds. Bring every row that is not mid-edit in line
	// with it, so a change made outside since the file was last read is shown
	// instead of being papered over by a stale row. A row the file leaves out
	// follows the key it inherits from.
	void Model::Sync()
	{
		for (auto& row : _rows) {
			const auto* entry = _doc.Find(row.key);
			row.inFile = entry != nullptr;
			if (!entry && !row.inherit.empty()) {
				entry = _doc.Find(row.inherit);
			}
			if (!entry) {
				continue;
			}
			Scalar disk = AsControl(row.control, entry->value);
			// a value whose type changed waits for the next Refresh, which
			// rebuilds the row with a control that fits it
			if (KindOf(disk) != KindOf(row.saved) || Identical(disk, row.saved)) {
				continue;
			}
			const bool clean = !row.Dirty();
			row.saved = disk;
			if (clean) {
				row.edit = std::move(disk);
			}
		}
	}

	void Model::ResetToDefault(const std::vector<std::size_t>& a_rows)
	{
		for (const auto index : a_rows) {
			if (index >= _rows.size() || !_rows[index].shipped) {
				continue;
			}
			auto& row = _rows[index];
			row.edit = AsControl(row.control, *row.shipped);
			row.failed.reset();
		}
	}
}
