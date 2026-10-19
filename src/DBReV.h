#pragma once

// Dragonborn ReVoiced (DBReV) voices the player: when the player picks a
// dialogue topic it plays the line from its voice pack and holds the
// conversation until the line is said. A line the pack has no recording for
// it offers to a VOICE PROVIDER, another SKSE plugin, through the exports of
// DBReV.dll (include\API\DBReV_API.h). This plugin is one: it takes such a
// line, generates it in the player's voice and plays it at the player, mouth
// moving (Speaker::SayPlayerLine), and tells DBReV when the line starts and
// how long it is. DBReV keeps the dialogue open for that long plus the user's
// post-line delay, handles skips and new topic clicks, and announces the line
// to camera and expression mods as it does its own. Nothing links against
// DBReV.dll: without it, or with one older than the provider API, nothing
// here does anything.
//
// One line, from this side:
//   onLineMissing   main thread, the game waits on it: the line is taken
//                   (Speaker queues it) or declined, and the call returns
//   started         the audio plays (PlayerTicket): DBReV_VoiceLineStarted,
//                   whose false means the line is gone and is stopped again
//   dropped         the line will not be spoken: DBReV_VoiceLineFailed, so
//                   the conversation moves on at once instead of waiting
//   onLineEnded     main thread, once per line taken: DBReV let the
//                   conversation go on; anything but "completed" stops the line
//
// DBReV also announces every player line it plays (its own and ours) as an
// SKSE message; that is listened to so that an NPC line spoken late stops when
// the player begins to speak, as the game stops its own lines.
namespace DBReV
{
	// kPostLoad: the broadcast listener. DBReV.dll must be loaded by then, and
	// before kPostLoad it may not be.
	void Listen();

	// kDataLoaded, and again after a config reload: registers this plugin as
	// the voice provider when [player] enable is on and DBReV offers the API.
	// Registering again replaces the earlier registration (the wait time may
	// have changed). Posted to the game thread.
	void Register();

	bool IsPresent();     // DBReV.dll is loaded
	bool IsRegistered();  // and this plugin is its voice provider
	std::string Status();  // one line, for the console
}
