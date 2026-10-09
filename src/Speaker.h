#pragma once

// Turns text into speech in the running game.
//
// A line goes to one background worker, which generates the audio (TtsEngine),
// writes it into a placeholder wav the game indexed at launch, and then asks
// AudioUtil, on the game thread, to play that file.
//
// Why placeholder files: Skyrim only plays loose files it saw at launch, but it
// re-reads a file's bytes on every play. So the plugin ships a few silent wavs
// in Data\Sound\AudioUtilTTS and overwrites one per line. AudioUtil itself
// needs nothing special for this: it goes through its public C exports.
//
// Two lanes, which share neither files nor a channel:
//   narration  flat, no speaker, no mouth     _tts_slotNN.wav    [playback]
//   speech     at an actor, their mouth moves _tts_speechNN.wav  [speech]
// Inside a lane the files are used round-robin on that lane's one AudioUtil
// channel, so the file being rewritten belongs to a line the channel stopped
// several lines ago. That is why the lanes must not share a channel.
//
// Lines arrive through the AudioUtilTTS natives or through mod events, which
// need no dependency on this mod:
//   AudioUtilTTS_Speak[:preset]  narration, strArg = text
//   AudioUtilTTS_Say[:preset]    the SENDING actor says strArg
// When a line starts playing, AudioUtilTTS_Spoken is sent back (strArg = text,
// numArg = the AudioUtil handle, sender = the speaker or none).
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

	// Narration. false = nothing queued (disabled, not ready, or no speakable
	// text). a_voice < 0, a_volume < 0, a_speed <= 0 take the configured defaults.
	bool Speak(std::string_view a_text, int a_voice, float a_volume, float a_speed);

	// Narration through a named preset. An empty or unknown name uses the defaults.
	bool SpeakAs(std::string_view a_text, std::string_view a_preset);
	bool HasPreset(std::string_view a_preset);

	// a_actor says the line: positioned at them, mouth moving when a_lipSync and
	// [speech] lipsync allow. An empty preset picks "female" / "male" by the
	// actor's sex when the config defines those.
	bool SpeakActor(RE::Actor* a_actor, std::string_view a_text, std::string_view a_preset, bool a_lipSync);

	bool IsReady();  // narration can be spoken right now
	bool IsBusy();
	void SetEnabled(bool a_enabled);
	bool IsEnabled();
	int  VoiceCount();  // voices in the default model, 0 while it is not loaded

	std::string Status();         // one line, for the console
	std::string Channel();        // the AudioUtil channel narration plays on
	std::string SpeechChannel();  // the AudioUtil channel character speech plays on
}
