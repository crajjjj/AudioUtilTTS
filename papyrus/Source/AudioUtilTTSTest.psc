Scriptname AudioUtilTTSTest Hidden
{Console harness for AudioUtilTTS. Exposed as real console commands through ConsoleUtil Extended, see SKSE\CustomConsole\AudioUtilTTSTest.yaml. In game: autts say "Some text"}

; Speak a line with the configured voice.
;   autts say "The road to Whiterun lay open."
string Function Say(string text) global
    if AudioUtilTTS.Speak(text)
        return "queued"
    endif
    return "not queued - " + AudioUtilTTS.GetStatus()
EndFunction

; The same through an explicit voice number.
;   autts sayv "Hello there." 9
string Function SayVoice(string text, int voice) global
    if AudioUtilTTS.Speak(text, voice)
        return "queued with voice " + voice + " of " + AudioUtilTTS.GetVoiceCount()
    endif
    return "not queued - " + AudioUtilTTS.GetStatus()
EndFunction

; The same through the mod event other mods use.
;   autts event "Hello there."
string Function ViaEvent(string text) global
    Game.GetPlayer().SendModEvent("AudioUtilTTS_Speak", text)
    return "event sent"
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
        return "config reloaded, model reloading"
    endif
    return "config did not parse, previous settings kept (see AudioUtilTTS.log)"
EndFunction

;   autts enable 0
string Function Enable(int on) global
    AudioUtilTTS.SetEnabled(on != 0)
    return AudioUtilTTS.GetStatus()
EndFunction
