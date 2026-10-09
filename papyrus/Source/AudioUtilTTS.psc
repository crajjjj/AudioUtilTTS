Scriptname AudioUtilTTS Hidden
{Live text-to-speech. Hand it a line of text and it is spoken in game a moment later, through AudioUtil.}

; ---------------------------------------------------------------------------
; The simplest way to use this from another mod needs none of the functions
; below and no compile-time dependency: send a mod event from any Form.
;
;     SendModEvent("AudioUtilTTS_Speak", "The road to Whiterun lay open.")
;     SendModEvent("AudioUtilTTS_Speak:whisper", "Not so loud.")
;
; The first is spoken with the configured default voice. The second names a
; preset after the colon: a voice the user defines in AudioUtilTTS.toml as
; [preset.whisper] (a model, a voice number, a speed and a volume under one
; name). An unknown preset name falls back to the default voice, so a mod can
; ask for a role without knowing what the user has installed.
;
; Those two are narration: flat, with no speaker. To make a CHARACTER say a
; line, let that actor send AudioUtilTTS_Say instead. The line is positioned at
; them and their mouth moves:
;
;     akSpeaker.SendModEvent("AudioUtilTTS_Say", "Not so loud.")
;     akSpeaker.SendModEvent("AudioUtilTTS_Say:whisper", "Not so loud.")
;
; With no preset named, the speaker gets the preset "female" or "male" by their
; sex when the config defines those.
;
; When AudioUtilTTS is not installed the events simply go nowhere. Use
; SendModEvent (the Form function), not the ModEvent.Create / Send pair.
;
; Call the natives directly when you need a voice number, volume or speed per
; line, or want to know whether a line was accepted. Guard the calls so they
; never run without the plugin:
;
;     If SKSE.GetPluginVersion("AudioUtilTTS") >= 0
;         AudioUtilTTS.SpeakAs("Not so loud.", "whisper")
;     EndIf
;
; When a line starts playing, the mod event AudioUtilTTS_Spoken fires:
; strArg = the text, numArg = the AudioUtil instance handle (works with
; AudioUtil.StopHandle, IsHandlePlaying, SetHandleVolume, GetHandleDuration),
; sender = the speaking actor, or None for narration.
; ---------------------------------------------------------------------------

; API version of the installed plugin. 1 = first release.
int Function GetAPIVersion() global native

; True when a line can be spoken right now: the default model is loaded,
; AudioUtil is present, a playable slot exists and speech is switched on.
bool Function IsReady() global native

; Queue a line on the default model. Returns false when nothing was queued (not
; ready, switched off, or no speakable text). Arguments below zero take the
; configured defaults. The line is generated in the background, so it starts a
; moment later: about two seconds for a sentence with the default model. A line
; that arrives while others wait replaces the oldest waiting one (see
; [playback] max_pending).
bool Function Speak(string asText, int aiVoice = -1, float afVolume = -1.0, float afSpeed = -1.0) global native

; Queue a line through a named preset from AudioUtilTTS.toml. A preset can name
; a different model; that model is loaded the first time a line asks for it
; (the line is about a second later that once) and then stays in memory. An
; empty or unknown preset name uses the default voice.
bool Function SpeakAs(string asText, string asPreset) global native

; Make an actor say a line: positioned at them, on the character channel, with
; their mouth moving when abLipSync is true and [speech] lipsync is on. The
; mouth follows the loudness of the line, it does not form real phonemes, and
; AudioUtil's own guards still apply (a gag, a worn tongue, a dialogue with the
; player and a mouth claim all keep it still). An empty asPreset picks "female"
; or "male" by the speaker's sex when the config defines those. The line is
; dropped if the speaker is gone or not loaded by the time it is ready.
bool Function SpeakActor(Actor akSpeaker, string asText, string asPreset = "", bool abLipSync = true) global native

; True when AudioUtilTTS.toml defines [preset.<asPreset>].
bool Function HasPreset(string asPreset) global native

; Drop every line that is waiting or being generated. The line already playing
; carries on; StopSpeaking silences that too.
Function CancelPending() global native

; True while a line is waiting or being generated.
bool Function IsBusy() global native

; Number of voices in the default model (0 while it is not loaded).
int Function GetVoiceCount() global native

; Session switch on top of [general] enable. Switching off drops waiting lines.
Function SetEnabled(bool abEnabled) global native
bool Function IsEnabled() global native

; Re-read AudioUtilTTS.toml and reload the models. False = the file did not
; parse and the previous settings were kept.
bool Function ReloadConfig() global native

; One line of status text: "ready | model ..." or "not ready: <why>".
string Function GetStatus() global native

; The AudioUtil channel narration plays on ([playback] channel).
string Function GetChannel() global native

; The AudioUtil channel character speech plays on ([speech] channel).
string Function GetSpeechChannel() global native

; Drop waiting lines and cut whatever is being spoken, narration and characters.
Function StopSpeaking() global
    CancelPending()
    AudioUtil.StopChannel(GetChannel())
    AudioUtil.StopChannel(GetSpeechChannel())
EndFunction
