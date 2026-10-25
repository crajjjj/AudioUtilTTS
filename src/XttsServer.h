#pragma once

// SkyrimNet's lines, spoken in this plugin's voices.
//
// SkyrimNet (AI dialogue for NPCs) sends every line an NPC says to a TTS
// server, and one kind it talks to is an XTTS server, by default at
// http://localhost:8020. The plugin answers there itself (XttsHttp), so
// SkyrimNet with XTTS as its voice engine speaks in the voices installed here,
// by voice type, the way unvoiced dialogue is spoken (Speaker::RenderLine),
// with no server to install or run. SkyrimNet plays the audio; the plugin only
// makes it.
//
// It listens only while SkyrimNet.dll is loaded and [skyrimnet] enable is on,
// on 127.0.0.1 only. A port that another program holds already is left to it:
// that is another XTTS server, which SkyrimNet then goes on using.
//
// Voice cloning ([skyrimnet] voice_cloning, off unless switched on): SkyrimNet
// can send a voice sample for a speaker (create_and_store_latents), which is
// kept in Data\SKSE\Plugins\AudioUtilTTS\skyrimnet\ and spoken like on the
// Pocket model [skyrimnet] clone_model. While it is off, such a request is
// answered 404, and SkyrimNet names its speakers by voice type instead.
namespace XttsServer
{
	// kDataLoaded, and whenever the config was read again: opens, closes or
	// moves the endpoint to match it. Never blocks: the work is done on a
	// thread of its own.
	void Apply();

	// One line: where SkyrimNet's lines are answered and how many were, or why
	// they are not.
	std::string Status();
}
