#pragma once

// The AudioUtilTTS pages in SKSE Menu Framework's Mod Control Panel: a Status
// page (what is installed and loaded, a voice to try, reload, the cache) and one
// page per part of AudioUtilTTS.toml, drawn from Core::Model. The menu edits
// that one file in place, comments and all, and the plugin applies a change at
// once (Speaker::ApplyConfig). The framework is optional: without it nothing
// here runs and the plugin is the same as before.

namespace Menu
{
	// Called once, at kDataLoaded, after Speaker::Start. Does nothing (and logs
	// why) when the framework is missing or lacks a function the menu calls.
	void Register();
}
