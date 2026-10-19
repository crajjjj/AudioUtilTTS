// DBReV_API.h — public SKSE interop contract for Dragonborn ReVoiced.
//
// This header may be freely used, modified and redistributed, including in
// closed-source projects. It is a declaration only: it contains no DBReV code,
// requires no linking against DBReV.dll, and has no dependency beyond <cstdint>.
//
// It describes two independent things:
//
//   BROADCASTS. DBReV announces every voiced dialogue line the player speaks, as an
//   SKSE message and as a Papyrus mod event. Strictly one-way: DBReV pushes, you
//   receive.
//
//       SKSE::GetMessagingInterface()->RegisterListener(DBReV::kSender, Callback);
//
//   VOICE PROVIDERS. A plugin that can voice the player's lines DBReV has no file
//   for (live TTS, for one) registers through DBReV.dll's exports and is handed
//   those lines. See "VOICE PROVIDERS" below. A plugin that only listens needs none
//   of it, and nothing about the broadcasts depends on it.
//
// See INTEGRATION.md. The three things that will bite a listener, in order:
//   1. Register from kPostLoad, not from plugin load (SKSE load order).
//   2. The callback runs on the MAIN thread (since DBReV 1.5.0). Before that it ran on
//      whichever thread happened to dispatch -- the Papyrus VM, or one of DBReV's own
//      worker threads -- so touching game state directly in the handler was a data race.
//      Bouncing through SKSE::GetTaskInterface()->AddTask remains correct and is still the
//      safest thing to do; it is simply no longer required to avoid corrupting the game.
//   3. Every pointer below dies when the callback returns.
//
// ABI: append-only. Fields are never reordered, resized or removed; new fields
// are added at the end and `version` is incremented. Check `version` (and, if
// you like, `structSize`) before reading anything.

#pragma once

#include <cstdint>

namespace DBReV
{
    // The SKSE plugin name to register against. A true return from
    // RegisterListener(kSender, ...) means DBReV is present in this load order;
    // that is the supported way to detect it. Do not test for DBReV.esp.
    inline constexpr const char* kSender = "DBReV";

    enum : std::uint32_t
    {
        // data -> const PlayerLineStart*
        // Sent immediately after the engine has been asked to speak the player's
        // line, i.e. the audio is starting now.
        kPlayerLineStart = 1,

        // data -> const PlayerLineEnd*
        // Sent once per kPlayerLineStart, when the dialogue actually advances.
        kPlayerLineEnd = 2,
    };

    enum : std::uint32_t
    {
        kEndReason_Completed  = 0,  // the line played to its measured end
        kEndReason_Skipped    = 1,  // the player skipped it (Smart Talk, Enter-to-advance)
        kEndReason_Superseded = 2,  // a new topic was clicked before this one finished

        // Voice providers only. These are passed to VoiceProvider::onLineEnded and never
        // appear in a PlayerLineEnd: a line that ends this way was never announced.
        kEndReason_TimedOut   = 3,  // the provider did not start it within maxWaitSeconds
        kEndReason_Cancelled  = 4,  // the Dialogue Menu closed before it started
    };

    struct PlayerLineStart
    {
        std::uint32_t version;       // 1, or 2 when voicedBy is present (see below)
        std::uint32_t structSize;    // size as DBReV filled it: 56 at version 1, 64 at version 2

        // Measured length of the audio, in seconds, parsed from the FUZ/xWMA
        // header before playback. 0.0 means DBReV has no length for this line,
        // and then only totalSeconds is meaningful. That happens when the file
        // could not be measured (totalSeconds is then an estimate), and when the
        // pack has no file for the line at all: nothing is playing, and
        // totalSeconds is only the user's post-line delay.
        float audioSeconds;

        // audioSeconds plus the user's configured post-line delay. This is how
        // long DBReV will hold the dialogue open before advancing, so it is the
        // number to hold a shot for.
        float totalSeconds;

        // Index of the clicked topic, as the engine's TopicClicked delegate
        // reported it. Matches position in MenuTopicManager::dialogueList.
        std::uint32_t topicIndex;

        // Size of lipData in bytes; 0 when the FUZ carried no LIP chunk.
        std::uint32_t lipSize;

        // The line's voice file, relative to Data/Sound/ (the path Player.SpeakSound
        // would be given). UTF-8. Empty when a voice provider voiced the line.
        const char* voicePath;

        // Active voice pack, e.g. "NG:my_pack" or "DBVO:my_pack". May be empty.
        const char* voicePackId;

        // Sanitized topic text, as used to build the filename. May be empty.
        const char* topicKey;

        // The LIP chunk lifted verbatim out of the FUZ container: the authored
        // phoneme track the engine itself would use for lip sync. nullptr when
        // absent. DBReV does not parse or validate this — it is the raw bytes.
        const void* lipData;

        // ---- version 2 ------------------------------------------------------
        // Read only when version >= 2.
        //
        // Name of the voice provider that voiced this line (see VOICE PROVIDERS),
        // e.g. "AudioUtilTTS". Such a line has no voicePath and no lipData: the
        // provider plays the audio and moves the mouth itself.
        //
        // DBReV sends the LOWEST version that carries everything a line has. A
        // line DBReV plays from its own file has nothing past version 1, so it is
        // still sent as version 1 at 56 bytes, exactly as before this field
        // existed; only a provider's line is version 2. Read a version-1 payload
        // as voicedBy == "": DBReV played its own file.
        const char* voicedBy;
    };
    static_assert(sizeof(PlayerLineStart) == 64, "PlayerLineStart ABI changed");

    struct PlayerLineEnd
    {
        std::uint32_t version;     // currently 1
        std::uint32_t structSize;  // sizeof(PlayerLineEnd) as DBReV built it
        std::uint32_t topicIndex;  // matches the PlayerLineStart it closes
        std::uint32_t reason;      // kEndReason_Completed, _Skipped or _Superseded
    };
    static_assert(sizeof(PlayerLineEnd) == 16, "PlayerLineEnd ABI changed");

    // ---------------------------------------------------------------------------
    // PAPYRUS MOD EVENTS
    //
    // The messages above are C++ only. The same two moments are also announced as
    // SKSE mod events, for Papyrus consumers -- facial animation, gestures, camera
    // work -- sent on the main thread from the same dispatch as the message:
    //
    //   PlayerVoice-LineStart   strArg = voice pack id ("NG:pack", "DBVO:pack", "")
    //                           numArg = the line's audio length in seconds
    //                           sender = the player
    //
    //   PlayerVoice-LineEnd     strArg = "completed" | "skipped" | "superseded"
    //                           numArg = topic index
    //                           sender = the player
    //
    //     Event OnPlayerVoiceLineStart(string eventName, string strArg, float numArg, Form sender)
    //
    // The names carry no mod's identity on purpose. A consumer that wants to leave
    // the player's mouth alone while it is moving should not have to know which
    // voice mod is installed, so any player-voice mod is free to send these and a
    // consumer that handles them handles all of them.
    //
    // A consumer should treat numArg on the start event as a deadline and release
    // the mouth when it passes, rather than waiting on the end event alone -- that
    // way a lost end event (crash, unload, a listener that registered mid-line)
    // cannot strand it.
    // ---------------------------------------------------------------------------

    // ---------------------------------------------------------------------------
    // VOICE PROVIDERS
    //
    // A voice pack covers the lines it covers. When the player picks a topic whose
    // line the active pack has no .fuz for, DBReV can hand that line to a voice
    // provider, another SKSE plugin (live TTS, typically), instead of letting it
    // pass in silence. DBReV then holds the dialogue while the provider produces
    // the audio and times the conversation to it exactly as it does to its own:
    // the user's post-line delay is added, skips work, and every listener above
    // sees an ordinary player line, with voicedBy set.
    //
    // Who owns what, for a line the provider takes:
    //   provider  the audio, and the player's mouth while it plays. DBReV plays
    //             nothing, applies no boost or effect, drives no lip sync and makes
    //             no AudioUtil mouth claim for that line.
    //   DBReV     the dialogue pause, skips, and the broadcasts.
    //
    // Only lines the active pack has no file for are offered: a line DBReV can
    // play is never handed out. With no voice pack selected DBReV stays out of
    // the conversation entirely, so nothing is offered then either.
    //
    // This half is DLL exports, not SKSE messages, because it needs answers.
    // Resolve them at run time, on or after kPostLoad; nothing links against
    // DBReV.dll:
    //
    //   HMODULE h   = GetModuleHandleA(DBReV::kModuleName);  // null: no DBReV
    //   auto    reg = h ? reinterpret_cast<DBReV::RegisterVoiceProviderFn>(
    //                         GetProcAddress(h, DBReV::kExport_RegisterVoiceProvider))
    //                   : nullptr;                            // null: DBReV predates providers
    //
    // One line, from the provider's side:
    //
    //   onLineMissing(line)               MAIN THREAD. Return true to voice it, false
    //                                     to leave it. Return quickly: the game waits
    //                                     on this call. Every pointer in `line` dies
    //                                     when you return, so copy what you keep.
    //
    //   DBReV_VoiceLineStarted(id, secs)  ANY THREAD. Your audio for the line starts
    //                                     now and runs `secs` (> 0). True: DBReV holds
    //                                     the dialogue for secs + postLineDelay and
    //                                     announces the line. False: the line is gone
    //                                     (skipped, superseded, timed out, menu closed),
    //                                     so do not play it, or stop it if it started.
    //                                     Calling it from inside onLineMissing, for a
    //                                     line you can play at once, counts as taking it.
    //
    //   DBReV_VoiceLineFailed(id)         ANY THREAD. Instead of the above, when you took
    //                                     the line and cannot voice it after all. Has no
    //                                     effect once the line has started.
    //
    //   onLineEnded(id, reason)           MAIN THREAD. Exactly once for every line you
    //                                     took, unless you failed it: DBReV has stopped
    //                                     holding it. For any reason but
    //                                     kEndReason_Completed, stop your audio now.
    //
    // A line nobody voices (no provider; one that declines; one that fails) is
    // handled exactly as DBReV has always handled a missing file, broadcasts
    // included. A listener cannot tell a provider is installed except by the lines
    // it actually voices.
    //
    // One provider at a time. A plugin registering under a different name while
    // one is registered is refused; registering again under the same name
    // replaces the callbacks. Neither callback may throw.
    // ---------------------------------------------------------------------------

    inline constexpr const char* kModuleName = "DBReV.dll";

    // What DBReV_GetInterfaceVersion returns for everything below.
    inline constexpr std::uint32_t kInterfaceVersion = 1;

    // Passed to VoiceProvider::onLineMissing.
    struct MissingLine
    {
        std::uint32_t version;     // currently 1
        std::uint32_t structSize;  // sizeof(MissingLine) as DBReV built it

        // Names this line in every call about it. Never 0, never reused in a session.
        std::uint32_t lineId;

        // As in PlayerLineStart: the clicked topic's position in
        // MenuTopicManager::dialogueList.
        std::uint32_t topicIndex;

        // Runtime FormIDs of the clicked TESTopicInfo and of its TESTopic, or 0 when
        // unknown. They hold for the session, not across load orders: key anything
        // kept on disk by the text, or by plugin name plus local ID.
        std::uint32_t topicInfoFormID;
        std::uint32_t topicFormID;

        // Seconds DBReV adds after your audio before the conversation moves on: the
        // user's post-line delay. For information; DBReV applies it.
        float postLineDelay;

        // How long DBReV will hold the dialogue waiting for DBReV_VoiceLineStarted
        // once you have taken the line, before giving up on it with
        // kEndReason_TimedOut. Your VoiceProvider::maxWaitSeconds, after clamping.
        float maxWaitSeconds;

        // What the player says: the topic text exactly as the dialogue menu shows it,
        // in the game's own encoding (the bytes a Papyrus string would hold). Stage
        // directions such as "(Persuade)" are still in it; leave out what should not
        // be spoken.
        const char* text;

        // Sanitized topic text, as in PlayerLineStart::topicKey.
        const char* topicKey;

        // Active voice pack, e.g. "NG:my_pack" or "DBVO:my_pack", so a provider can
        // pick its voice to suit the pack.
        const char* voicePackId;

        // The last file DBReV looked for, relative to Data/Sound/. It does not exist;
        // this is for your log.
        const char* missingPath;
    };
    static_assert(sizeof(MissingLine) == 64, "MissingLine ABI changed");

    // Passed to DBReV_RegisterVoiceProvider. Copied, so it need not outlive the call.
    struct VoiceProvider
    {
        std::uint32_t version;     // 1
        std::uint32_t structSize;  // sizeof(VoiceProvider)

        // Your plugin's short name, e.g. "AudioUtilTTS". Shows in DBReV.log and in
        // PlayerLineStart::voicedBy. Required.
        const char* name;

        // Both required. See the sequence above.
        bool (*onLineMissing)(const MissingLine* a_line);
        void (*onLineEnded)(std::uint32_t a_lineId, std::uint32_t a_reason);

        // How long the dialogue may wait for your audio once you have taken a line,
        // in seconds. 0 = DBReV's default (10). Clamped to 1-30. The player sits in
        // a silent conversation for all of it, so a provider that can be slow does
        // better to take only the lines it can start soon.
        float maxWaitSeconds;
    };
    static_assert(sizeof(VoiceProvider) == 40, "VoiceProvider ABI changed");

    // DBReV.dll's exports: extern "C", so undecorated on x64. Resolve each with
    // GetProcAddress(GetModuleHandleA(kModuleName), kExport_...) and null-check it.
    //
    //   GetInterfaceVersion    kInterfaceVersion or higher.
    //   RegisterVoiceProvider  true: registered. false: an incomplete VoiceProvider,
    //                          or a different provider already holds the slot.
    //   VoiceLineStarted       see the sequence above. secs <= 0 counts as failing.
    //   VoiceLineFailed        see the sequence above.
    using GetInterfaceVersionFn   = std::uint32_t (*)();
    using RegisterVoiceProviderFn = bool (*)(const VoiceProvider* a_provider);
    using VoiceLineStartedFn      = bool (*)(std::uint32_t a_lineId, float a_audioSeconds);
    using VoiceLineFailedFn       = void (*)(std::uint32_t a_lineId);

    inline constexpr const char* kExport_GetInterfaceVersion   = "DBReV_GetInterfaceVersion";
    inline constexpr const char* kExport_RegisterVoiceProvider = "DBReV_RegisterVoiceProvider";
    inline constexpr const char* kExport_VoiceLineStarted      = "DBReV_VoiceLineStarted";
    inline constexpr const char* kExport_VoiceLineFailed       = "DBReV_VoiceLineFailed";
}
