Scriptname AudioUtilTTS Hidden
{Live text-to-speech. Hand it a line of text and it is spoken in game a moment later, through AudioUtil.}

; ---------------------------------------------------------------------------
; The simplest way to use this from another mod needs none of the functions
; below and no compile-time dependency: send a mod event from any Form. The
; event name is AudioUtilTTS_Speak:<preset>:<sender>.
;
;     SendModEvent("AudioUtilTTS_Speak::MyMod", "The road to Whiterun lay open.")
;     SendModEvent("AudioUtilTTS_Speak:whisper:MyMod", "Not so loud.")
;
; <sender> is your mod's own short name. It goes into AudioUtilTTS.log with the
; line, so a user can see which mod a spoken or dropped line came from; nothing
; else depends on it. <preset> names a voice the user defines in
; AudioUtilTTS.toml as [preset.whisper] (a model, a voice number, a speed and a
; volume under one name). Left empty, as in the first line, it means the
; configured default voice. An unknown preset name falls back to the default
; voice too, so a mod can ask for a role without knowing what the user has
; installed.
;
; Those two are narration: flat, with no speaker. To make a CHARACTER say a
; line, let that actor send AudioUtilTTS_Say:<preset>:<sender> instead. The
; line is positioned at them and their mouth moves:
;
;     akSpeaker.SendModEvent("AudioUtilTTS_Say::MyMod", "Not so loud.")
;     akSpeaker.SendModEvent("AudioUtilTTS_Say:whisper:MyMod", "Not so loud.")
;
; With no preset named, the speaker gets the voice of their voice type when the
; user has set one up (see [voicetype] in AudioUtilTTS.toml), else the preset
; "female" or "male" by their sex when the config defines those.
;
; DEPRECATED since API 3: the event names without a sender, AudioUtilTTS_Speak
; and AudioUtilTTS_Speak:<preset> (the same for AudioUtilTTS_Say). They still
; work, but their lines carry no name in the log. A plugin older than API 3
; reads ":MyMod" as part of the preset name, finds no such preset and speaks
; the line with the default voice. For AudioUtilTTS_Say that is the default
; voice in place of the speaker's own, and a voice type the user set never to
; be spoken for is spoken for too. Check GetAPIVersion() >= 3 where that
; matters.
;
; Game dialogue needs no call at all: a dialogue line that has no recorded
; voice file is generated and spoken by the plugin on its own, in the voice of
; the speaker's voice type. See [dialogue] in AudioUtilTTS.toml and the
; dialogue functions at the end of this file.
;
; When AudioUtilTTS is not installed the events simply go nowhere. Use
; SendModEvent (the Form function), not the ModEvent.Create / Send pair.
;
; Call the natives directly when you want to know whether a line was accepted.
; Guard the calls so they never run without the plugin:
;
;     If SKSE.GetPluginVersion("AudioUtilTTS") >= 0
;         AudioUtilTTS.SpeakFrom("MyMod", "Not so loud.", "whisper")
;     EndIf
;
; When a line starts playing, the mod event AudioUtilTTS_Spoken fires:
; strArg = the text, numArg = the AudioUtil instance handle (works with
; AudioUtil.StopHandle, IsHandlePlaying, SetHandleVolume, GetHandleDuration),
; sender = the speaking actor, or None for narration.
; ---------------------------------------------------------------------------

; API version of the installed plugin. 1 = first release. 2 added the dialogue
; functions and GetActorVoice. 3 added SpeakFrom, SpeakActorFrom,
; IsNarrationFree, IsSpeechFree and the sender name in the event names.
int Function GetAPIVersion() global native

; True when the plugin can speak right now: a voice model is installed (the
; default one or any other), AudioUtil is present, a playable slot exists and
; speech is switched on. The default model is optional since 0.3.1: without it
; a Speak call returns false for a line whose voice has no model of its own,
; so check what the call returns.
bool Function IsReady() global native

; Queue a narration line. asSender is your mod's own short name ("MyMod"); it
; is written into AudioUtilTTS.log with the line, so a user can see which mod a
; spoken or dropped line came from. Nothing else depends on it. asPreset names
; a preset from AudioUtilTTS.toml; empty or unknown means the default voice. A
; preset can name a different model: that model is loaded the first time a line
; asks for it (the line is about a second later that once) and then stays in
; memory.
; Returns false when nothing was queued: not ready, switched off, no speakable
; text, or another line is in the way. The line is generated in the background,
; so it starts a moment later: about two seconds for a sentence with the
; default model. A line that arrives while another narration line is being
; generated or spoken is dropped and noted in AudioUtilTTS.log; character lines
; are counted on their own in the same way. [playback] max_pending lets lines
; wait instead. IsNarrationFree tells beforehand. (API 3)
bool Function SpeakFrom(string asSender, string asText, string asPreset = "") global native

; Make an actor say a line: positioned at them, on the character channel, with
; their mouth moving when abLipSync is true and [speech] lipsync is on. The
; mouth follows the loudness of the line, it does not form real phonemes, and
; AudioUtil's own guards still apply (a gag, a worn tongue, a dialogue with the
; player and a mouth claim all keep it still). asSender is as for SpeakFrom. An
; empty asPreset picks the voice of the speaker's voice type when one is set up
; (GetActorVoice tells which), else "female" or "male" by the speaker's sex
; when the config defines those. The line is dropped if the speaker is gone or
; not loaded by the time it is ready. IsSpeechFree tells whether a character
; line would be taken. (API 3)
bool Function SpeakActorFrom(string asSender, Actor akSpeaker, string asText, string asPreset = "", bool abLipSync = true) global native

; DEPRECATED since API 3: use SpeakFrom. Narration with no sender name, by a
; voice number of the default model; arguments below zero take the configured
; defaults. A voice, volume or speed of your own is what a preset is for.
; Kept, and working as before, for scripts compiled against it.
bool Function Speak(string asText, int aiVoice = -1, float afVolume = -1.0, float afSpeed = -1.0) global native

; DEPRECATED since API 3: use SpeakFrom. The same line with no sender name.
; Kept, and working as before, for scripts compiled against it.
bool Function SpeakAs(string asText, string asPreset) global native

; DEPRECATED since API 3: use SpeakActorFrom. The same line with no sender
; name. Kept, and working as before, for scripts compiled against it.
bool Function SpeakActor(Actor akSpeaker, string asText, string asPreset = "", bool abLipSync = true) global native

; Which voice akSpeaker speaks with when no preset is named, and why, as one
; line of text: "MaleNord: preset 'malenord' (model my-voices, voice 12)".
; (API 2)
string Function GetActorVoice(Actor akSpeaker) global native

; True when AudioUtilTTS.toml defines [preset.<asPreset>].
bool Function HasPreset(string asPreset) global native

; Drop every narration and character line that is waiting or being generated.
; The line already playing carries on; StopSpeaking silences that too. Dialogue
; lines being prepared are the game's business and are not touched.
Function CancelPending() global native

; True while a narration or character line is waiting or being generated.
; It turns false once the line starts playing: to ask whether a new line would
; be taken, use the two below.
bool Function IsBusy() global native

; Would a narration line (SpeakFrom, AudioUtilTTS_Speak) or a character line
; (SpeakActorFrom, AudioUtilTTS_Say) sent right now be taken? False while the
; plugin is not ready, and, with the default
; [playback] max_pending = 0, while another line of that kind is being
; generated or spoken. A script that paces its own lines waits for this, not
; for IsBusy. True is no promise: another mod can take the lane a moment
; later, so still check what the Speak call returns. (API 3)
; Neither knows which voice the line will ask for. IsNarrationFree takes it to
; be the default voice, so it is false while the default model is not
; installed (a line through a preset on another model is still taken).
; IsSpeechFree cannot tell whether the speaker's voice has a model: without the
; default model, SpeakActorFrom returns false for a speaker who has none.
bool Function IsNarrationFree() global native
bool Function IsSpeechFree() global native

; Number of voices in the default model (0 while it is not loaded).
int Function GetVoiceCount() global native

; Session switch on top of [general] enable. Switching off drops waiting lines.
Function SetEnabled(bool abEnabled) global native
bool Function IsEnabled() global native

; Re-read AudioUtilTTS.toml and reload the models. False = the file did not
; parse and the previous settings were kept.
bool Function ReloadConfig() global native

; One line of status text, for people to read. It starts with "ready | model
; ..." when the default model is loaded, with "ready | no default model (<why>)
; ..." when only other models are installed (since 0.3.1), with "switched off"
; after SetEnabled(false), or with "not ready: <why>". To test for a working
; install in a script, use IsReady, not this text.
string Function GetStatus() global native

; The AudioUtil channel narration plays on ([playback] channel).
string Function GetChannel() global native

; The AudioUtil channel character speech plays on ([speech] channel).
string Function GetSpeechChannel() global native

; Drop waiting lines and cut whatever is being spoken, narration and characters.
; A dialogue line the game is already saying is the game's own and carries on.
Function StopSpeaking() global
    CancelPending()
    AudioUtil.StopChannel(GetChannel())
    AudioUtil.StopChannel(GetSpeechChannel())
EndFunction

; ---------------------------------------------------------------------------
; Dialogue (API 2). Nothing here has to be called for dialogue to be spoken:
; the plugin does that by itself when [dialogue] enable is true. A dialogue
; line the game is about to say that has no recorded voice file is generated in
; the voice of the speaker's voice type and played by the game like a recorded
; one, with its subtitles, volume and lipsync. Lines with a recorded voice are
; never touched.
; ---------------------------------------------------------------------------

; Session switch on top of [dialogue] enable. It cannot switch the feature on
; when the config had it off at game start: the game is only patched then.
Function SetDialogueEnabled(bool abEnabled) global native

; True when unvoiced dialogue is being spoken: enabled in the config, patched
; into the game and not switched off.
bool Function IsDialogueEnabled() global native

; One line of status text: on or off and why, lines voiced and left unvoiced
; this session, and the size of the cache of generated lines.
string Function GetDialogueStatus() global native

; Delete every cached dialogue line, so each is generated afresh the next time
; it is said. Returns the number of files removed. Not needed after replacing a
; model: lines are cached per model file, and ReloadConfig picks up the change.
int Function ClearDialogueCache() global native
