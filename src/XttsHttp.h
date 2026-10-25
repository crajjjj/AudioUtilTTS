#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The HTTP side of the XTTS endpoint SkyrimNet talks to: the requests an XTTS
// server answers (xtts-api-server, and langfod's skyrimnet-tts which SkyrimNet
// is set up for), with nothing of the game in it, so tests\xtts_tests.cpp can
// drive it over a real socket. XttsServer.cpp wires it to the plugin.
//
//   POST /tts_to_audio/             JSON {text, speaker_wav, language, ...}
//                                   -> the line as audio/wav
//   POST /create_and_store_latents  multipart: speaker_name, language, wav_file
//                                   -> a voice sample to speak like
//   GET  /health                    -> JSON, the server is up
//
// SkyrimNet sends the text "ping" to wake a server up: that gets a short
// silence at once. A 404 from create_and_store_latents is how an XTTS server
// says it takes no voice samples; SkyrimNet then names its speakers instead.
namespace XttsHttp
{
	struct SpeakRequest
	{
		std::string text;
		std::string speaker;   // SpeakerName of speaker_wav: "malenord"
		std::string language;  // as sent, "" = not given
		// false once the client has gone away: a handler waiting for a line can
		// give it up. Cheap enough to poll a few times a second.
		std::function<bool()> connected;
	};

	struct SpeakResult
	{
		int               status = 200;
		std::vector<char> wav;    // the body when status is 200
		std::string       error;  // the detail otherwise
	};

	struct CloneRequest
	{
		std::string speaker;  // SpeakerName of speaker_name
		std::string language;
		std::string fileName;  // as the client named the upload
		std::string bytes;     // the wav file
	};

	struct CloneResult
	{
		int         status = 200;
		std::string error;
	};

	struct Handlers
	{
		// Both run on the server's own threads and may block while a line is
		// made: each request has a thread of its own, up to a few at once.
		std::function<SpeakResult(const SpeakRequest&)> speak;
		std::function<CloneResult(const CloneRequest&)> clone;
		std::function<void(bool a_warn, const std::string&)> log;
	};

	// One listening socket and the threads that answer it.
	class Server
	{
	public:
		Server();
		~Server();  // Stop
		Server(const Server&) = delete;
		Server& operator=(const Server&) = delete;

		// Listens on a_host:a_port. false with a_error when the port cannot be
		// had: anything already answers a connection to it (another server, also
		// one listening on every address, which a bind alone would not notice),
		// or the bind fails. Only one Start at a time: Stop first.
		bool Start(const std::string& a_host, int a_port, Handlers a_handlers, std::string& a_error);
		// Closes the socket and waits for the requests being answered to end.
		void Stop();
		bool Running() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> _impl;
	};

	// The name a speaker is known by: speaker_wav without its folder and its
	// extension, lowercase ("malenord" from "speakers\en\MaleNord.wav"). Only
	// ASCII letters, digits, '_', '-' and '.' are kept, so it can name a file; a
	// name with letters outside ASCII gets a hash of the whole name after them
	// instead, so that two such names stay two speakers.
	std::string SpeakerName(std::string_view a_speakerWav);

	// a_ms of silence as a mono 16-bit wav
	std::vector<char> Silence(int a_rate, int a_ms);
}
