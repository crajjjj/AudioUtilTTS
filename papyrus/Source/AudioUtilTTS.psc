Scriptname AudioUtilTTS Hidden
{Live text-to-speech. Hand it a line of text and it is spoken in game a moment later, through AudioUtil.}

; ---------------------------------------------------------------------------
; The simplest way to use this from another mod needs none of the functions
; below and no compile-time dependency: send a mod event from any Form.
;
;     SendModEvent("AudioUtilTTS_Speak", "The road to Whiterun lay open.")
;
; It is spoken with the configured voice. When AudioUtilTTS is not installed
; the event simply goes nowhere. Use SendModEvent (the Form function), not the
; ModEvent.Create / Send pair.
;
; Call the natives directly when you need a voice, volume or speed per line, or
; want to know whether a line was accepted. Guard the calls so they never run
; without the plugin:
;
;     If SKSE.GetPluginVersion("AudioUtilTTS") >= 0
;         AudioUtilTTS.Speak("The road to Whiterun lay open.", 9)
;     EndIf
;
; When a line starts playing, the mod event AudioUtilTTS_Spoken fires:
; strArg = the text, numArg = the AudioUtil instance handle (works with
; AudioUtil.StopHandle, IsHandlePlaying, SetHandleVolume, GetHandleDuration).
; ---------------------------------------------------------------------------

; API version of the installed plugin. 1 = first release.
int Function GetAPIVersion() global native

; True when a line can be spoken right now: the model is loaded, AudioUtil is
; present, a playable slot exists and speech is switched on.
bool Function IsReady() global native

; Queue a line. Returns false when nothing was queued (not ready, switched off,
; or no speakable text). Arguments below zero take the configured defaults.
; The line is generated in the background, so it starts a moment later: about
; two seconds for a sentence with the default model. A line that arrives while
; others wait replaces the oldest waiting one (see [playback] max_pending).
bool Function Speak(string asText, int aiVoice = -1, float afVolume = -1.0, float afSpeed = -1.0) global native

; Drop every line that is waiting or being generated. The line already playing
; carries on; StopSpeaking silences that too.
Function CancelPending() global native

; True while a line is waiting or being generated.
bool Function IsBusy() global native

; Number of voices in the loaded model (0 while none is loaded).
int Function GetVoiceCount() global native

; Session switch on top of [general] enable. Switching off drops waiting lines.
Function SetEnabled(bool abEnabled) global native
bool Function IsEnabled() global native

; Re-read AudioUtilTTS.toml and reload the model. False = the file did not
; parse and the previous settings were kept.
bool Function ReloadConfig() global native

; One line of status text: "ready | model ..." or "not ready: <why>".
string Function GetStatus() global native

; The AudioUtil channel speech plays on ([playback] channel).
string Function GetChannel() global native

; Drop waiting lines and cut the line being spoken.
Function StopSpeaking() global
    CancelPending()
    AudioUtil.StopChannel(GetChannel())
EndFunction
