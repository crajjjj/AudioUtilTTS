// The text replacement matching (src\TextReplace.h), no game, no SKSE:
//   xmake build text-tests && xmake run text-tests
#include "TextReplace.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
	int g_failed = 0;

	void Check(const std::string& a_got, const std::string& a_want, const char* a_what)
	{
		if (a_got != a_want) {
			std::cout << "FAIL: " << a_what << ": got \"" << a_got << "\", want \"" << a_want << "\"\n";
			++g_failed;
		}
	}

	using Rules = std::vector<TextReplace::Rule>;
}

int main()
{
	const Rules ube{ { "UBE", "" } };
	Check(TextReplace::Apply("How about a good time with a Nord UBE?", ube), "How about a good time with a Nord ?",
		"a word is dropped, the surrounding text stays");
	Check(TextReplace::Apply("a tube and a cube", ube), "a tube and a cube", "not inside another word");
	Check(TextReplace::Apply("ube Ube UBE", ube), "  ", "ASCII case is ignored");
	Check(TextReplace::Apply("UBE", ube), "", "the whole text");
	Check(TextReplace::Apply("", ube), "", "empty text");

	const Rules race{ { "Nord UBE", "Nord" }, { "Dark Elf UBE", "Dunmer" } };
	Check(TextReplace::Apply("They asked for a Dark Elf UBE, a Nord UBE and a nord ube.", race),
		"They asked for a Dunmer, a Nord and a Nord.", "phrases, every occurrence");
	Check(TextReplace::Apply("Nord UBEs", race), "Nord UBEs", "a longer word is not the phrase");

	const Rules sex{ { "(Sex)", "" } };
	Check(TextReplace::Apply("Come here. (Sex)", sex), "Come here. ", "a phrase with no letter at its ends needs no boundary");

	const Rules order{ { "a", "b" }, { "b", "c" } };
	Check(TextReplace::Apply("a b", order), "c c", "rules apply in order, each over the whole text");

	const Rules utf8{ { "Ol\xC3\xA1", "Hello" } };
	Check(TextReplace::Apply("Ol\xC3\xA1 world, ol\xC3\xA1!", utf8), "Hello world, Hello!",
		"ASCII case is ignored around bytes outside ASCII");
	Check(TextReplace::Apply("Ol\xC3\xA2 world", utf8), "Ol\xC3\xA2 world", "bytes outside ASCII must match exactly");
	Check(TextReplace::Apply("Nordic", Rules{ { "Nord", "X" } }), "Nordic", "not at the start of a word");
	Check(TextReplace::Apply("Nord's axe", Rules{ { "Nord", "Imperial" } }), "Nord's axe",
		"an apostrophe binds the word");
	Check(TextReplace::Apply("aaa", Rules{ { "aa", "b" } }), "aaa", "no match inside a longer run of word bytes");
	Check(TextReplace::Apply("x aa aa", Rules{ { "aa", "b" } }), "x b b", "matches do not overlap");
	Check(TextReplace::Apply("keep", Rules{ { "", "gone" } }), "keep", "an empty phrase is ignored");

	std::cout << (g_failed ? "FAILED" : "OK") << ": " << g_failed << " failed\n";
	return g_failed ? 1 : 0;
}
