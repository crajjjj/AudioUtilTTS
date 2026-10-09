#pragma once

// Turns text into speech in the running game.
//
// A line handed to Speak goes to one background worker, which generates the
// audio (TtsEngine), writes it into a placeholder wav the game indexed at
// launch, and then asks AudioUtil, on the game thread, to play that file.
//
// Why placeholder files: Skyrim only plays loose files it saw at launch, but it
// re-reads a file's bytes on every play. So the plugin ships a few silent wavs
// (Data\Sound\AudioUtilTTS\_tts_slotNN.wav) and overwrites one per line. They
// are used round-robin on a single AudioUtil channel, so the file being
// rewritten is never the one still playing. AudioUtil itself needs no change:
// this goes through its public AudioUtil_PlayFile export.
//
// Lines arrive two ways: the AudioUtilTTS natives, and the mod event
// AudioUtilTTS_Speak (strArg = text), which needs no dependency on this mod.
// "AudioUtilTTS_Speak:<preset>" speaks through a named preset. When a line
// starts playing, AudioUtilTTS_Spoken is sent back (strArg = text, numArg = the
// AudioUtil handle).
//
// Presets ([preset.<name>] in the config) name a model + voice + speed +
// volume. The default model is loaded at start; any other model a preset names
// is loaded the first time a line asks for it and then kept, up to
// [model] max_loaded (least recently used goes first, the default never does).
namespace Speaker
{
	void Start();   // kDataLoaded: slots, AudioUtil lookup, worker (loads the default model)
	bool Reload();  // re-read the config and reload the models; false = config kept
	void Cancel();  // drop every line waiting or being generated

	// false = nothing queued (disabled, not ready, or no speakable text).
	// a_voice < 0, a_volume < 0, a_speed <= 0 take the configured defaults.
	bool Speak(std::string_view a_text, int a_voice, float a_volume, float a_speed);

	// Speak through a named preset. An empty or unknown name uses the defaults.
	bool SpeakAs(std::string_view a_text, std::string_view a_preset);
	bool HasPreset(std::string_view a_preset);

	bool IsReady();
	bool IsBusy();
	void SetEnabled(bool a_enabled);
	bool IsEnabled();
	int  VoiceCount();  // voices in the default model, 0 while it is not loaded

	std::string Status();   // one line, for the console
	std::string Channel();  // the AudioUtil channel speech plays on
}
