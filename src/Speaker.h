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
// Two lanes go through AudioUtil, and share neither files nor a channel:
//   narration  flat, no speaker, no mouth     _tts_slotNN.wav    [playback]
//   speech     at an actor, their mouth moves _tts_speechNN.wav  [speech]
// Inside a lane the files are used round-robin on that lane's one AudioUtil
// channel, so the file being rewritten belongs to a line the channel stopped
// several lines ago. That is why the lanes must not share a channel.
//
// A third lane does not: dialogue. When the game is about to say a response
// that has no recorded voice file (DialogueHook), the line is generated in the
// voice of the speaker's voice type and the game is handed a placeholder under
// Sound\Voice\AudioUtilTTS (_tts_dialogNN.wav, with a .lip beside it) as that
// response's voice file. The game then plays the line itself, like any
// recorded one: its timing, subtitles, volume slider and lipsync all apply.
// Generated lines are kept in a disk cache, so each is generated once.
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
	// [speech] lipsync allow. An empty preset picks the voice of the actor's
	// voice type ([voicetype], a preset or a model folder named like it), else
	// "female" / "male" by their sex when the config defines those.
	bool SpeakActor(RE::Actor* a_actor, std::string_view a_text, std::string_view a_preset, bool a_lipSync);

	// Dialogue. Called by DialogueHook, on the thread building the response,
	// for a response whose voice file does not exist. Returns the voice file the
	// game should play instead ("Data\Sound\Voice\AudioUtilTTS\..."), or "" to
	// leave the response as the game built it (unvoiced). a_response is the
	// response's number inside its topic info (1 = the one said first) and
	// a_info that topic info, used only to tell lines of one topic from another.
	//
	// A line said before comes from the cache at once. A new one depends on
	// whether the game is about to say it or is only listing it:
	//   - The game builds the responses of every topic in the dialogue menu up
	//     front. Those are generated in the background, first responses first,
	//     and the game never waits; by the time the player picks one it is
	//     usually in place.
	//   - A line said right away (a scene, a greeting, a remark in passing) is
	//     generated while the caller waits, up to [dialogue] wait_ms; later
	//     responses of its topic are generated while the earlier ones play.
	// A line that cannot be ready in time stays unvoiced this once and is
	// generated for the next time.
	std::string VoiceDialogue(std::string_view a_text, const RE::BGSVoiceType* a_voiceType, int a_response,
		const void* a_info);

	void SetDialogueHooked(bool a_hooked);    // DialogueHook reports whether its patch is in place
	void SetDialogueEnabled(bool a_enabled);  // session switch on top of [dialogue] enable
	bool IsDialogueEnabled();                 // hooked, switched on and enabled in the config
	std::string DialogueStatus();             // one line, for the console
	std::string DescribeVoice(RE::Actor* a_actor);  // which voice this actor speaks with, and why
	int         ClearDialogueCache();         // files removed

	bool IsReady();  // narration can be spoken right now
	bool IsBusy();
	void SetEnabled(bool a_enabled);
	bool IsEnabled();
	int  VoiceCount();  // voices in the default model, 0 while it is not loaded

	std::string Status();         // one line, for the console
	std::string Channel();        // the AudioUtil channel narration plays on
	std::string SpeechChannel();  // the AudioUtil channel character speech plays on
}
