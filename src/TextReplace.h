#pragma once

// Words and phrases replaced in a line before it is spoken: what a mod writes
// is not always what should be read out (a race renamed "Nord UBE" by a body
// mod, an abbreviation, a name the voice mispronounces). The rules come from
// replacements\*.toml (see Config::Settings::replacements); this is the
// matching, with no game or config dependency so it can be tested on its own.
//
// A rule matches whole: not inside another word, so "UBE" leaves "tube"
// alone. A phrase that begins or ends with something other than a letter or
// digit needs no word boundary on that side ("(Sex)" matches as written).
// ASCII case is ignored; bytes outside ASCII (UTF-8 sequences) must match
// exactly and count as letters. Rules apply in the order given, each over the
// whole text, left to right without overlap.

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace TextReplace
{
	struct Rule
	{
		std::string from;
		std::string to;
	};

	inline bool WordByte(unsigned char a_c)
	{
		return a_c >= 0x80 || std::isalnum(a_c) != 0 || a_c == '_' || a_c == '\'';
	}

	inline bool SameNoCase(unsigned char a_a, unsigned char a_b)
	{
		return a_a == a_b || (a_a < 0x80 && a_b < 0x80 && std::tolower(a_a) == std::tolower(a_b));
	}

	inline std::string Apply(std::string_view a_text, const std::vector<Rule>& a_rules)
	{
		std::string out(a_text);
		for (const auto& rule : a_rules) {
			if (rule.from.empty() || out.empty()) {
				continue;
			}
			const bool needBefore = WordByte(static_cast<unsigned char>(rule.from.front()));
			const bool needAfter = WordByte(static_cast<unsigned char>(rule.from.back()));
			std::string next;
			next.reserve(out.size());
			std::size_t i = 0;
			while (i < out.size()) {
				bool match = i + rule.from.size() <= out.size();
				for (std::size_t k = 0; match && k < rule.from.size(); ++k) {
					match = SameNoCase(static_cast<unsigned char>(out[i + k]), static_cast<unsigned char>(rule.from[k]));
				}
				if (match) {
					const std::size_t end = i + rule.from.size();
					const bool        before = i == 0 || !WordByte(static_cast<unsigned char>(out[i - 1]));
					const bool        after = end == out.size() || !WordByte(static_cast<unsigned char>(out[end]));
					match = (before || !needBefore) && (after || !needAfter);
				}
				if (match) {
					next += rule.to;
					i += rule.from.size();
				} else {
					next += out[i];
					++i;
				}
			}
			out = std::move(next);
		}
		return out;
	}
}
