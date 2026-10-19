Scriptname AudioUtilTTSTest Hidden
{Console harness for AudioUtilTTS. Exposed as real console commands through ConsoleUtil Extended, see SKSE\CustomConsole\AudioUtilTTSTest.yaml. In game: autts say "Some text"}

; Why a line was not taken. With the plugin ready, the usual reason is a line
; still being generated or spoken: a second one is dropped, not queued (see
; [playback] max_pending), and the log says so. The other one is a voice whose
; model is not installed, which the status line shows ("no default model").
string Function NotQueued() global
    if AudioUtilTTS.IsReady()
        return "not queued - another line is still being generated or spoken, or the voice has no model (see AudioUtilTTS.log) | " + AudioUtilTTS.GetStatus()
    endif
    return "not queued - " + AudioUtilTTS.GetStatus()
EndFunction

; Speak a line with the configured voice.
;   autts say "The road to Whiterun lay open."
string Function Say(string text) global
    if AudioUtilTTS.Speak(text)
        return "queued"
    endif
    return NotQueued()
EndFunction

; The same through an explicit voice number of the default model.
;   autts sayv "Hello there." 9
string Function SayVoice(string text, int voice) global
    if AudioUtilTTS.Speak(text, voice)
        return "queued with voice " + voice + " of " + AudioUtilTTS.GetVoiceCount()
    endif
    return NotQueued()
EndFunction

; The same through a named preset from AudioUtilTTS.toml.
;   autts sayas "Not so loud." whisper
string Function SayAs(string text, string preset) global
    string note = ""
    if !AudioUtilTTS.HasPreset(preset)
        note = " (preset '" + preset + "' is not defined, default voice used)"
    endif
    if AudioUtilTTS.SpeakAs(text, preset)
        return "queued" + note
    endif
    return NotQueued()
EndFunction

; The same, naming the sender: the name shows up in AudioUtilTTS.log.
;   autts sayfrom "Not so loud." MyMod
string Function SayFrom(string text, string sender) global
    if AudioUtilTTS.SpeakFrom(sender, text)
        return "queued from " + sender
    endif
    return NotQueued()
EndFunction

; Would a narration line and a character line be taken right now?
;   autts free
string Function Free() global
    return "narration free: " + AudioUtilTTS.IsNarrationFree() + " | character speech free: " + AudioUtilTTS.IsSpeechFree()
EndFunction

; A line through the mod event other mods use.
;   autts event "Hello there."
string Function ViaEvent(string text) global
    Game.GetPlayer().SendModEvent("AudioUtilTTS_Speak", text)
    return "event sent"
EndFunction

; A line through the mod event, naming a preset.
;   autts eventas "Not so loud." whisper
string Function ViaEventAs(string text, string preset) global
    Game.GetPlayer().SendModEvent("AudioUtilTTS_Speak:" + preset, text)
    return "event AudioUtilTTS_Speak:" + preset + " sent"
EndFunction

; Make a character say a line: the reference selected in the console, or the
; player when nothing is selected (third person to see the mouth). The voice is
; the one set up for the speaker's voice type, else the preset "female" or
; "male" by their sex (autts voice tells which).
;   autts sayactor "Not so loud."
string Function SayActor(string text) global
    return SayActorAs(text, "")
EndFunction

; The same with a named preset.
;   autts sayactoras "Not so loud." whisper
string Function SayActorAs(string text, string preset) global
    Actor speaker = Game.GetCurrentConsoleRef() as Actor
    if !speaker
        speaker = Game.GetPlayer()
    endif
    if AudioUtilTTS.SpeakActor(speaker, text, preset)
        return "queued for " + speaker.GetDisplayName()
    endif
    return NotQueued()
EndFunction

; The same through the mod event, sent by that character.
;   autts sayevent "Not so loud."
string Function SayEvent(string text) global
    Actor speaker = Game.GetCurrentConsoleRef() as Actor
    if !speaker
        speaker = Game.GetPlayer()
    endif
    speaker.SendModEvent("AudioUtilTTS_Say", text)
    return "event AudioUtilTTS_Say sent by " + speaker.GetDisplayName()
EndFunction

; Which voice the console-selected actor (or the player) speaks with, and why:
; their voice type and the preset or model it leads to. The same voice their
; unvoiced dialogue lines get.
;   autts voice
string Function Voice() global
    Actor speaker = Game.GetCurrentConsoleRef() as Actor
    if !speaker
        speaker = Game.GetPlayer()
    endif
    return speaker.GetDisplayName() + " - " + AudioUtilTTS.GetActorVoice(speaker)
EndFunction

; Unvoiced game dialogue: show the status, or switch it for this session.
;   autts dialogue        status
;   autts dialogue 0      off
;   autts dialogue 1      on
string Function Dialogue(int on) global
    if on >= 0
        AudioUtilTTS.SetDialogueEnabled(on != 0)
    endif
    return AudioUtilTTS.GetDialogueStatus()
EndFunction

; Your own lines, with Dragonborn ReVoiced: show the status, or switch them
; for this session.
;   autts player          status
;   autts player 0        off
;   autts player 1        on
string Function Player(int on) global
    if on >= 0
        AudioUtilTTS.SetPlayerEnabled(on != 0)
    endif
    return AudioUtilTTS.GetPlayerStatus()
EndFunction

; Delete the cached dialogue lines, so each is generated afresh.
;   autts clearcache
string Function ClearCache() global
    return "cached lines removed: " + AudioUtilTTS.ClearDialogueCache()
EndFunction

;   autts status
string Function Status() global
    return AudioUtilTTS.GetStatus()
EndFunction

;   autts stop
string Function Stop() global
    AudioUtilTTS.StopSpeaking()
    return "stopped"
EndFunction

;   autts reload
string Function Reload() global
    if AudioUtilTTS.ReloadConfig()
        return "config reloaded, models reloading"
    endif
    return "config did not parse, previous settings kept (see AudioUtilTTS.log)"
EndFunction

;   autts enable 0
string Function Enable(int on) global
    AudioUtilTTS.SetEnabled(on != 0)
    return AudioUtilTTS.GetStatus()
EndFunction
