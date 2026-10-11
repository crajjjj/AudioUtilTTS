#pragma once

// Makes greetings ahead of time ([dialogue] prepare_greetings).
//
// A greeting is said the moment the game decides on it, so a line that has to
// be generated first cannot be ready: it stays unvoiced the first time and is
// spoken from the second (see Speaker::VoiceDialogue). This closes that gap for
// the lines most often said that way. While the player walks around, the NPCs
// close by are looked at one after another: which greetings would this one say
// right now? Those the game has no voice file for are generated for the cache,
// behind everything the game actually asked for, and are simply there when the
// NPC does speak.
//
// "Which greetings" is the game's own answer as far as it can be had without
// the game saying anything: the greeting topics of running quests, highest
// quest priority first, and in each the first topic info whose conditions hold
// for that NPC (a run of "random" infos counts as one draw). The responses are
// then built by the game's own response builder, so the text and the voice
// file names are exactly what a real greeting gets, other mods' changes
// included. The dialogue hook sees them being built and, told that this is a
// rehearsal (DialogueHook::Preparing), only passes them on.
//
// It is a guess: conditions can change before the NPC speaks, and most NPCs
// walked past never greet. So it is kept cheap. A few lines per NPC, each NPC
// looked at once every few minutes, each line built once per session, the
// condition checks spread over frames, and nothing while the game is paused,
// in a menu or in combat.
namespace Greetings
{
	// kDataLoaded, after the dialogue hook went in: collects the greeting
	// topics and starts the ticker. Does nothing while the setting is off, but
	// "autts reload" can switch it on later.
	void Start();

	// A save is being loaded: forget who was looked at.
	void Reset();
}
