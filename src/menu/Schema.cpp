#include "menu/Schema.h"

namespace Core
{
	namespace
	{
		std::vector<std::string_view> Segments(std::string_view a_path)
		{
			std::vector<std::string_view> parts;
			std::size_t                   start = 0;
			while (true) {
				const auto dot = a_path.find('.', start);
				parts.push_back(a_path.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
				if (dot == std::string_view::npos) {
					return parts;
				}
				start = dot + 1;
			}
		}
	}

	bool Matches(std::string_view a_pattern, std::string_view a_key, std::string* a_matched)
	{
		const auto pattern = Segments(a_pattern);
		const auto key = Segments(a_key);
		if (pattern.size() != key.size()) {
			return false;
		}
		std::string_view matched;
		for (std::size_t i = 0; i < pattern.size(); ++i) {
			if (pattern[i] == "*") {
				if (key[i].empty()) {
					return false;
				}
				matched = key[i];
			} else if (pattern[i] != key[i]) {
				return false;
			}
		}
		if (a_matched) {
			*a_matched = std::string{ matched };
		}
		return true;
	}

	std::string Substitute(std::string_view a_text, std::string_view a_matched)
	{
		std::string out;
		out.reserve(a_text.size() + a_matched.size());
		for (const char c : a_text) {
			if (c == '*') {
				out += a_matched;
			} else {
				out += c;
			}
		}
		return out;
	}

	Schema::Schema(std::vector<PageSpec> a_pages, std::vector<SettingSpec> a_settings) :
		_pages(std::move(a_pages)),
		_settings(std::move(a_settings))
	{}

	const SettingSpec* Schema::Find(std::string_view a_key, std::string* a_matched, std::size_t* a_index) const
	{
		for (std::size_t i = 0; i < _settings.size(); ++i) {
			if (_settings[i].key == a_key) {
				if (a_matched) {
					a_matched->clear();
				}
				if (a_index) {
					*a_index = i;
				}
				return &_settings[i];
			}
		}
		for (std::size_t i = 0; i < _settings.size(); ++i) {
			if (_settings[i].key.find('*') != std::string::npos && Matches(_settings[i].key, a_key, a_matched)) {
				if (a_index) {
					*a_index = i;
				}
				return &_settings[i];
			}
		}
		return nullptr;
	}

	int Schema::PageTitled(std::string_view a_title) const
	{
		for (std::size_t i = 0; i < _pages.size(); ++i) {
			if (_pages[i].title == a_title) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	int Schema::PageOf(std::string_view a_section) const
	{
		for (std::size_t i = 0; i < _pages.size(); ++i) {
			for (const auto& section : _pages[i].sections) {
				if (section == a_section || (section.find('*') != std::string::npos && Matches(section, a_section))) {
					return static_cast<int>(i);
				}
			}
		}
		return -1;
	}
}
