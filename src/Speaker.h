#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <vector>

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
// Three lanes go through AudioUtil, and share neither files nor a channel:
//   narration  flat, no speaker, no mouth     _tts_slotNN.wav    [playback]
//   speech     at an actor, their mouth moves _tts_speechNN.wav  [speech]
//   player     the player's own line in a     _tts_playerNN.wav  [player]
//              conversation (see PlayerLine)
// Inside a lane the files are used round-robin on that lane's one AudioUtil
// channel, so the file being rewritten belongs to a line the channel stopped
// several lines ago. That is why the lanes must not share a channel.
//
// A fourth lane does not: dialogue. When the game is about to say a response
// that has no recorded voice file (DialogueHook), the line is generated in the
// voice of the speaker's voice type and the game is handed a placeholder under
// Sound\Voice\AudioUtilTTS (_tts_dialogNN.wav, with a .lip beside it) as that
// response's voice file. The game then plays the line itself, like any
// recorded one: its timing, subtitles, volume slider and lipsync all apply.
// Generated lines are kept in a disk cache, so each is generated once.
//
// Lines arrive through the AudioUtilTTS natives or through mod events, which
// need no dependency on this mod:
//   AudioUtilTTS_Speak:<preset>:<sender>  narration, strArg = text
//   AudioUtilTTS_Say:<preset>:<sender>    the SENDING actor says strArg
// <sender> is the sending mod's own name, for the log; the forms without it
// are deprecated and still taken.
// When a line starts playing, AudioUtilTTS_Spoken is sent back (strArg = text,
// numArg = the AudioUtil handle, sender = the speaker or none).
//
// Presets ([preset.<name>] in the config) name a model + voice + speed +
// volume. The default model is loaded at start; any other model a preset names
// is loaded the first time a line asks for it and then kept, up to
// [model] max_loaded (least recently used goes first, the default never does).
//
// The default model is optional. Without it, a line that names no other model
// is not taken (narration, the presets on the default model, the voices by
// sex), and a voice with a model of its own (a model folder named like a voice
// type, a model.toml, a voice type patch, a preset on another model) is spoken
// as usual.
namespace Speaker
{
	void Start();   // kDataLoaded: slots, AudioUtil lookup, worker (loads the default model, if there is one)
	bool Reload();  // re-read the config and reload the models; false = config kept

	// The in-game menu wrote AudioUtilTTS.toml: re-read it. Only a change to
	// what the models are loaded with ([general] enable, the [model] keys
	// except max_loaded) or to a placeholder count does what Reload does; any
	// other setting is read again for each line, so it applies to the next one
	// without dropping a line or reloading a voice.
	enum class ApplyResult
	{
		kApplied,   // the new settings are in use
		kReloaded,  // and the models are being loaded again
		kNotParsed  // the file did not parse: the previous settings are kept
	};
	ApplyResult ApplyConfig();
	void Cancel();  // drop every narration and character line waiting or being generated
	void Reset();   // Cancel, and drop every dialogue line on its way too: a save is being loaded

	// Narration. false = nothing queued (disabled, not ready, no speakable text,
	// or the lane is busy). a_voice < 0, a_volume < 0, a_speed <= 0 take the
	// configured defaults. Behind the deprecated Speak native only: it has no
	// sender name.
	bool Speak(std::string_view a_text, int a_voice, float a_volume, float a_speed);

	// Narration through a named preset. An empty or unknown name uses the defaults.
	// a_sender is the calling mod's own name, written into the log with the line
	// so that a spoken or dropped line can be traced to its mod; "" = not given.
	bool SpeakAs(std::string_view a_text, std::string_view a_preset, std::string_view a_sender = {});
	bool HasPreset(std::string_view a_preset);

	// a_actor says the line: positioned at them, mouth moving when a_lipSync and
	// [speech] lipsync allow. An empty preset picks the voice of the actor's
	// voice type ([voicetype], a preset named like it, a voice type patch, a model
	// that lists it in its model.toml or a model folder named like it), else
	// "female" / "male" by their sex when the config defines those.
	bool SpeakActor(RE::Actor* a_actor, std::string_view a_text, std::string_view a_preset, bool a_lipSync,
		std::string_view a_sender = {});

	// Dialogue. Called by DialogueHook, on the thread building the response,
	// for a response whose voice file does not exist. Returns the voice file the
	// game should play instead ("Data\Sound\Voice\AudioUtilTTS\..."), or "" to
	// leave the response as the game built it (unvoiced). a_response is the
	// response's number inside its topic info (1 = the one said first) and
	// a_info that topic info, used only to tell lines of one topic from another.
	//
	// What happens depends on whether the game is about to say the line or is
	// only listing it:
	//   - The game builds the responses of every topic in the dialogue menu up
	//     front. Those are put in place in the background, first responses
	//     first, and the game never waits: a cached line is copied into its
	//     placeholder, a new one is generated if that should take no longer
	//     than a player takes to pick a topic.
	//   - A line said right away (a scene, a greeting, a remark in passing): a
	//     cached one is copied into its placeholder there and then. A new one
	//     cannot be made in time, and the game is never kept waiting for it;
	//     only the later responses of its topic are generated while the earlier
	//     ones play. Greetings can be made ahead instead (Greetings.cpp).
	// A line that cannot be in place in time is left as the game built it this
	// once (unvoiced, for Fuz Ro D'oh to cover) and generated for the next time.
	// The exception is a line of one response ([dialogue] greetings; a greeting
	// is the plainest case, the line that opens a conversation another):
	// a_greeting, when not 0, is the form ID of the topic info of such a line. Such a
	// line is still left unvoiced for the game, but it is spoken as a character
	// line, a moment late, as soon as it is generated and the game has reported
	// who speaks it. It is generated ahead of every other waiting line, and
	// spoken unless its speaker has gone on to another line by then.
	// A line with nothing to say (a stage direction, markup, a placeholder such
	// as "...") is left alone altogether.
	// The one gap: a listed line that was expected in time and is picked before
	// it is in plays as silence, since its placeholder was already handed over.
	std::string VoiceDialogue(std::string_view a_text, const RE::BGSVoiceType* a_voiceType, int a_response,
		const void* a_info, RE::FormID a_greeting = 0);

	// Greetings made ahead (Greetings.cpp, [dialogue] prepare_greetings).
	//
	// The voice type a_actor's unvoiced lines are spoken for, or nullptr when
	// those lines would stay unvoiced (no voice set up for it, dialogue off).
	const RE::BGSVoiceType* DialogueVoiceType(RE::Actor* a_actor);
	// Generates a line for the cache only, behind every line the game asked for,
	// so that it is there when the game does say it. Never hands out a
	// placeholder and never makes anything wait. false = nothing queued: no
	// voice for that voice type, already cached or on its way, or the queue is
	// full.
	bool PrepareDialogue(std::string_view a_text, const RE::BGSVoiceType* a_voiceType, int a_response,
		const void* a_info);
	// Is there room for a_lines more of those? The queue holds only a few: ask
	// before working out what to prepare.
	bool CanPrepareDialogue(int a_lines);

	// The player's own line in a conversation, for a voice mod that holds the
	// conversation open while it is said (Dragonborn ReVoiced, see DBReV.cpp):
	// the mod offers a line it has no recording for; the plugin generates it in
	// the player's voice, plays it at the player with their mouth moving, and
	// tells the mod when it starts and how long it is. A line said before comes
	// from the cache and starts from inside the call. The voice is [player]
	// preset, else the preset "female" or "male" by the player's sex: never the
	// player's voice type, which is a game actor's recorded voice.
	struct PlayerLine
	{
		std::uint32_t id = 0;                 // the holder's name for the line, for the log
		std::string   holder;                 // the holder's name, for the log
		float         maxWaitSeconds = 0.0f;  // how long the holder waits for the line to start
		// Exactly one of the two is called, once. started: on the game thread,
		// the moment the line plays, with its length in seconds; false = the
		// holder has let the conversation go on meanwhile, and the line is
		// stopped again. dropped: on any thread, when a line that was taken will
		// not be spoken after all (cancelled, not generated, not played).
		// Neither may call back into Speaker.
		std::function<bool(float a_seconds)> started;
		std::function<void()>                dropped;
	};
	// Game thread. "" = taken: one of the two callbacks follows. Otherwise why
	// not, and neither is called; a line that is only too slow to be ready
	// within maxWaitSeconds is still generated for the cache, so it is said at
	// once the next time.
	std::string SayPlayerLine(std::string_view a_text, PlayerLine a_line);
	// The holder has let the conversation go on: the line is dropped where it
	// waits or is being generated, and, when a_stop (it did not play to its
	// end: skipped, superseded, timed out), stopped where it plays.
	void EndPlayerLine(std::uint32_t a_id, bool a_stop);
	// The player begins a line, any voice mod's: an NPC line being spoken late
	// by the player's conversation partner stops, as the game stops its own.
	void PlayerSpeaks();
	void SetPlayerEnabled(bool a_enabled);  // session switch on top of [player] enable
	bool IsPlayerEnabled();                 // switched on and enabled in the config
	std::string PlayerStatus();             // one line, for the console

	// A line for SkyrimNet (XttsServer.h), which plays the audio itself: made on
	// the worker and handed back as a wav file, never played here. Of the lines
	// still to be generated it goes first, but for the player's own, which a
	// conversation is held open for; only lines generated already and waiting
	// to play (greetings spoken late) are put in place before it. A narration
	// or character line waiting for its lane to fall quiet lets it through.
	//   a_voiceType  the speaker as SkyrimNet names it, a voice type's editor
	//                ID ("malenord", case ignored). Its voice is found as an
	//                unvoiced dialogue line's is, else it is the preset
	//                "female" / "male" by the voice type's sex (by the name
	//                when no voice type is called that), else the default voice.
	//   a_clip       when not empty: a recording to speak like instead, on the
	//                Pocket model [skyrimnet] clone_model; when that cannot be
	//                done, the voice of a_voiceType speaks after all
	// Waits for the line at most a_wait, and gives it up as soon as
	// a_stillWanted says no (the request's client went away). A line with
	// nothing to say, or for a voice type the config never speaks for, is a
	// moment of silence.
	struct Rendered
	{
		std::vector<char> wav;            // a mono 16-bit wav; empty = no line
		std::string       voice;          // which voice it is in, for the log
		std::string       error;          // why there is no line
		bool              later = false;  // not now (busy, loading, given up), rather than never
		std::size_t       chars = 0;
		float             seconds = 0.0f;
		long long         ms = 0;         // what generating it took
	};
	Rendered RenderLine(std::string_view a_text, std::string_view a_voiceType, const std::filesystem::path& a_clip,
		std::chrono::milliseconds a_wait, const std::function<bool()>& a_stillWanted);
	// Every SkyrimNet line waiting or being made is given up, and its request
	// answered at once.
	void DropRenderedLines();

	void SetDialogueHooked(bool a_hooked);    // DialogueHook reports whether its patch is in place
	void SetDialogueEnabled(bool a_enabled);  // session switch on top of [dialogue] enable
	bool IsDialogueEnabled();                 // hooked, switched on and enabled in the config
	std::string DialogueStatus();             // one line, for the console
	std::string DescribeVoice(RE::Actor* a_actor);  // which voice this actor speaks with, and why
	int         ClearDialogueCache();         // files removed

	// The plugin can speak right now: AudioUtil is there, a slot is playable and
	// a voice model is installed, the default one or any other. Which voice a
	// line will ask for is not known here, so a speak call can still return
	// false: its voice is on a model that is not there.
	bool IsReady();
	bool IsBusy();   // a narration or character line is waiting or being generated
	// Would a narration / character line sent right now be taken? false while
	// the plugin is not ready (see IsReady), and in one-at-a-time mode while
	// another line of that kind is being generated or spoken. Neither knows the
	// voice the line will ask for. Narration is taken to be in the default
	// voice, so IsNarrationFree is false without the default model; a
	// character's voice depends on the character, so IsSpeechFree leaves that
	// to the speak call's own answer.
	bool IsNarrationFree();
	bool IsSpeechFree();
	void SetEnabled(bool a_enabled);
	bool IsEnabled();
	int  VoiceCount();  // voices in the default model, 0 while it is not loaded

	std::string Status();         // one line, for the console

	// The few facts the in-game menu's Status page shows, as values. It counts
	// the files of the dialogue cache: call it off the game and render threads.
	struct Summary
	{
		std::string   notReady;              // why nothing can be spoken; "" = something can
		bool          switchedOff = false;   // speech is switched off for the session
		std::string   defaultModel;          // the [model] dir while it is loaded, else ""
		int           defaultVoices = 0;
		std::string   noDefaultWhy;          // why it is not loaded, when it is not
		int           modelsLoaded = 0;
		int           modelsInstalled = 0;
		std::string   dialogue;              // "on", or why it is off
		std::string   player;                // the player's lines: on and how many, or why not
		std::string   skyrimnet;             // SkyrimNet's lines: where they are answered and how many, or why not
		std::uint32_t given = 0;             // this session: responses given a voice
		std::uint32_t unvoiced = 0;          // left unvoiced
		std::uint32_t late = 0;              // spoken late
		std::size_t   cacheLines = 0;
		double        cacheMb = 0.0;
	};
	Summary GetSummary();
	std::string Channel();        // the AudioUtil channel narration plays on
	std::string SpeechChannel();  // the AudioUtil channel character speech plays on
}
