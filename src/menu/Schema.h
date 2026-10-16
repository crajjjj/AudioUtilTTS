#pragma once

// How the in-game menu presents each AudioUtilTTS.toml key: page, group, label,
// tip, control. Presentation only: it holds no values (those are the user's
// file) and no defaults (those are the shipped file). The table itself is
// MenuSchema.cpp, built into the DLL so that it cannot go missing or fall out
// of step with the plugin that reads the keys.

#include "menu/ConfigDoc.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core
{
	enum class Control
	{
		Auto,     // not said: picked from the value's TOML type
		Flag,     // a checkbox (a TOML boolean)
		Int,
		Percent,  // int 0..100 as a slider
		Float,
		Text
	};

	// When a change reaches the speech. A hint in the menu, nothing more: the
	// plugin decides by itself what a new config needs (Speaker::ApplyConfig).
	enum class Applies
	{
		Now,      // the next line uses it
		Reload,   // the voice models are loaded again first, a second or two
		Restart   // after the next game start
	};

	// The group a page shows last and closed: settings for an unusual setup.
	inline constexpr std::string_view kAdvancedGroup = "Advanced";

	struct SettingSpec
	{
		// "dialogue.fallback", or a pattern with one "*" for a name the file
		// chooses: "preset.*.voice" (any preset), "voicetype.*" (any voice type)
		std::string           key;
		std::string           group;  // a "*" in it is what the pattern matched: "Preset *"
		std::string           page;   // the title of the page it is shown on; empty = its table's page
		std::string           label;  // likewise; empty = made from the key's name
		std::string           tip;
		std::string           unit;
		Control               control = Control::Auto;
		Applies               applies = Applies::Now;
		std::optional<double> min;
		std::optional<double> max;
		std::optional<double> step;
		// For a pattern over tables ("preset.*.speed"): offer the key in every
		// such table of the user's file, also where the table leaves it out. Such
		// a row shows what the plugin uses instead: the value of `inherit`
		// ("voice.speed"), else `fallback`.
		bool                  everyTable = false;
		std::string           inherit;
		std::optional<Scalar> fallback;
	};

	struct PageSpec
	{
		std::string              title;
		std::string              note;      // one line under the title, optional
		std::vector<std::string> sections;  // the tables it edits; "preset.*" = every [preset.<name>]
	};

	class Schema
	{
	public:
		Schema() = default;
		Schema(std::vector<PageSpec> a_pages, std::vector<SettingSpec> a_settings);

		const std::vector<PageSpec>&    Pages() const { return _pages; }
		const std::vector<SettingSpec>& Settings() const { return _settings; }  // in the order rows appear

		// The spec of a key: an exact one first, then a pattern. a_matched gets
		// what the "*" stood for ("" for an exact match). a_index its place in
		// Settings().
		const SettingSpec* Find(std::string_view a_key, std::string* a_matched = nullptr, std::size_t* a_index = nullptr) const;

		// The page that edits a table, or -1 when no page names it.
		int PageOf(std::string_view a_section) const;
		// The page with this title, or -1.
		int PageTitled(std::string_view a_title) const;

	private:
		std::vector<PageSpec>    _pages;
		std::vector<SettingSpec> _settings;
	};

	// A "*" in a pattern stands for exactly one dotted segment. a_matched gets it.
	bool Matches(std::string_view a_pattern, std::string_view a_key, std::string* a_matched = nullptr);

	// Every "*" in a_text replaced by a_matched.
	std::string Substitute(std::string_view a_text, std::string_view a_matched);

	// The AudioUtilTTS menu (MenuSchema.cpp).
	const Schema& TtsSchema();
}
