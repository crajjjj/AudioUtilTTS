#pragma once

// What the menu shows: the user's AudioUtilTTS.toml joined with the schema (how
// to present a key) and the shipped file (what "reset" means). Pure C++: the
// ImGui side (Menu.cpp) only draws these rows and calls Commit. Adapted from SLO
// VE's settings menu (skse\src\core\Model), where a key missing from the file
// fell back to a script's own literal; here a missing key runs at the default
// the shipped file shows, so a missing key is nothing to warn about.

#include "menu/ConfigDoc.h"
#include "menu/Schema.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Core
{
	struct Row
	{
		std::string key;
		std::string section;
		std::string name;

		// presentation (from the schema, or made up for a key it does not know)
		std::string           label;
		std::string           tip;
		std::string           unit;
		Control               control = Control::Auto;  // resolved: never Auto after Load
		Applies               applies = Applies::Now;
		std::optional<double> min;
		std::optional<double> max;
		std::optional<double> step;
		bool                  known = false;  // the schema describes this key

		// values
		Scalar                saved;  // what the file holds; while it holds nothing, what the plugin uses
		Scalar                edit;   // what the widget holds; written when it differs from saved
		std::optional<Scalar> shipped;  // the shipped default, when there is one
		bool                  inFile = false;
		// while the file leaves the key out, the key whose value the plugin uses
		// instead ("voice.speed" for a preset's speed)
		std::string inherit;

		// a value the last write refused, so a failing edit is not retried every frame
		std::optional<Scalar> failed;

		bool Dirty() const { return !Identical(edit, saved); }
	};

	struct Group
	{
		std::string              title;
		std::vector<std::size_t> rows;  // indices into Model::Rows()
	};

	struct Page
	{
		std::string        title;
		std::string        note;
		std::vector<Group> groups;
	};

	class Model
	{
	public:
		// a_file: the user's AudioUtilTTS.toml (required). a_defaults: the shipped
		// copy (optional; without it there is no default and no reset). The pages
		// are the schema's, in its order, an empty one included, then a page
		// "Other" when the file holds a table no page names.
		// False when a_file cannot be read or parsed; Error() says why and the
		// model then has no rows.
		bool Load(const std::filesystem::path& a_file, const std::filesystem::path& a_defaults, const Schema& a_schema);

		// Re-read the user's file (the menu was opened: pick up hand edits). Edits
		// not yet written are dropped. True when the text on disk is not what this
		// model last read or wrote.
		bool Refresh();

		bool               Ok() const { return _doc.Ok(); }
		const std::string& Error() const { return _doc.Error(); }
		bool               HasDefaults() const { return _defaults.Ok(); }

		std::vector<Page>&       Pages() { return _pages; }
		const std::vector<Page>& Pages() const { return _pages; }
		std::vector<Row>&        Rows() { return _rows; }
		const std::vector<Row>&  Rows() const { return _rows; }
		Row*                     Find(std::string_view a_key);
		const ConfigDoc&         Doc() const { return _doc; }

		// Write the edit of every listed row that differs from the file, as one
		// exclusive read-splice-write of what is on disk NOW (ConfigDoc::Rewrite),
		// so a change made outside since the menu read the file survives. Each row
		// stands alone: one the file refuses remembers the value that failed
		// (Row::failed) and the others are still written. A key the file leaves
		// out is added under its table. Afterwards every row that is not mid-edit
		// shows what the file holds. False when anything listed could not be
		// written.
		bool Commit(const std::vector<std::size_t>& a_rows);

		// Add keys the file does not hold yet (a voice type line). A key that
		// appeared in the meantime is left as it is. The rows are built again
		// afterwards, so this is for a click, not for a frame with a field being
		// typed in. False when the file could not be written.
		bool Add(const std::vector<Edit>& a_edits);

		// Set each listed row's edit to its shipped default (rows without one are
		// left alone). Does not write: follow with Commit.
		void ResetToDefault(const std::vector<std::size_t>& a_rows);

		// how many writes reached the disk since Load
		std::size_t Writes() const { return _writes; }

	private:
		void Build();
		void Sync();

		std::filesystem::path _file;
		std::filesystem::path _defaultsFile;
		const Schema*         _schema = nullptr;
		ConfigDoc             _doc;
		ConfigDoc             _defaults;
		std::vector<Row>      _rows;
		std::vector<Page>     _pages;
		std::size_t           _writes = 0;
	};

	// "max_loaded" -> "Max loaded": the label of a key the schema does not describe
	std::string LabelFromName(std::string_view a_name);
}
