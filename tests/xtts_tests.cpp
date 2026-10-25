// The XTTS endpoint SkyrimNet talks to (src\XttsHttp.h), over a real socket,
// with an HTTP client sending what SkyrimNet sends. No game, no SKSE:
//   xmake build xtts-tests && xmake run xtts-tests
#include "XttsHttp.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <iostream>
#include <mutex>
#include <string>

namespace
{
	int g_failed = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		if (!a_ok) {
			std::cout << "FAIL: " << a_what << "\n";
			++g_failed;
		}
	}

	void Same(const std::string& a_got, const std::string& a_want, const std::string& a_what)
	{
		if (a_got != a_want) {
			std::cout << "FAIL: " << a_what << ": got \"" << a_got << "\", want \"" << a_want << "\"\n";
			++g_failed;
		}
	}

	constexpr int PORT = 18021;  // not SkyrimNet's 8020: an XTTS server may run there

	std::string Detail(const httplib::Result& a_res)
	{
		const auto body = nlohmann::json::parse(a_res->body, nullptr, false);
		return body.is_object() && body.contains("detail") && body["detail"].is_string() ? body["detail"].get<std::string>() :
		                                                                                    std::string{};
	}

	bool IsWav(const std::string& a_body)
	{
		return a_body.size() > 44 && a_body.compare(0, 4, "RIFF") == 0 && a_body.compare(8, 4, "WAVE") == 0;
	}
}

int main()
{
	// ------------------------------------------------------------ speaker names
	Same(XttsHttp::SpeakerName("malenord"), "malenord", "a bare name");
	Same(XttsHttp::SpeakerName("MaleNord.wav"), "malenord", "lowercase, without .wav");
	Same(XttsHttp::SpeakerName("speakers\\en\\FemaleNord.wav"), "femalenord", "a Windows path");
	Same(XttsHttp::SpeakerName("latents_pt/en/femaleeventoned.pt"), "femaleeventoned", "a path with slashes");
	Same(XttsHttp::SpeakerName("..\\..\\..\\Windows\\evil"), "evil", "never a way out of the folder");
	Same(XttsHttp::SpeakerName("..."), "", "dots alone are nothing");
	Same(XttsHttp::SpeakerName("Male Nord: old"), "malenordold", "only name characters are kept");
	Same(XttsHttp::SpeakerName(""), "", "no name");

	// a silence is a real wav
	const auto silence = XttsHttp::Silence(24000, 100);
	Check(silence.size() == 44 + 4800 && IsWav(std::string(silence.begin(), silence.end())), "100 ms of 24 kHz silence");

	// ------------------------------------------------------------------ server
	std::mutex                last;  // guards the two below
	XttsHttp::SpeakRequest    lastSpeak;
	XttsHttp::CloneRequest    lastClone;
	std::atomic<bool>         cloning{ false };
	XttsHttp::Handlers        handlers;
	handlers.speak = [&](const XttsHttp::SpeakRequest& a_request) {
		{
			std::scoped_lock lock{ last };
			lastSpeak = a_request;
		}
		if (a_request.speaker == "busy") {
			return XttsHttp::SpeakResult{ 503, {}, "not ready within 60 s" };
		}
		return XttsHttp::SpeakResult{ 200, XttsHttp::Silence(22050, 500), {} };
	};
	handlers.clone = [&](const XttsHttp::CloneRequest& a_request) {
		{
			std::scoped_lock lock{ last };
			lastClone = a_request;
		}
		return cloning.load() ? XttsHttp::CloneResult{ 200, {} } : XttsHttp::CloneResult{ 404, "voice cloning is off" };
	};

	XttsHttp::Server server;
	std::string      error;
	Check(server.Start("127.0.0.1", PORT, handlers, error), "the server starts: " + error);

	httplib::Client client("localhost", PORT);  // as SkyrimNet names it
	client.set_read_timeout(10);

	// what SkyrimNet sends for a line
	{
		const nlohmann::json body{ { "text", "Never should have come here." }, { "speaker_wav", "MaleNord" },
			{ "language", "en" }, { "save_path", "out.wav" } };
		const auto res = client.Post("/tts_to_audio/", body.dump(), "application/json");
		Check(res && res->status == 200, "a line is answered");
		if (res) {
			Same(res->get_header_value("Content-Type"), "audio/wav", "as audio/wav");
			Check(IsWav(res->body), "with a wav in it");
		}
		std::scoped_lock lock{ last };
		Same(lastSpeak.text, "Never should have come here.", "the text arrives whole");
		Same(lastSpeak.speaker, "malenord", "the speaker by name");
		Same(lastSpeak.language, "en", "the language");
	}
	// the path without its trailing slash, as some clients write it
	{
		const auto res = client.Post("/tts_to_audio", R"({"text":"Hi.","speaker_wav":"femalenord"})", "application/json");
		Check(res && res->status == 200, "the path without the slash is answered too");
	}
	// a text with escapes and UTF-8 in it
	{
		const nlohmann::json body{ { "text", "She said \"Ol\xC3\xA1\",\nthen left." }, { "speaker_wav", "femalenord" } };
		const auto           res = client.Post("/tts_to_audio/", body.dump(), "application/json");
		Check(res && res->status == 200, "a text with escapes is answered");
		std::scoped_lock lock{ last };
		Same(lastSpeak.text, "She said \"Ol\xC3\xA1\",\nthen left.", "and arrives as it was sent");
	}
	// SkyrimNet waking the server up
	{
		{
			std::scoped_lock lock{ last };
			lastSpeak = {};
		}
		const auto res = client.Post("/tts_to_audio/", R"({"text":"ping","speaker_wav":"malecommoner"})", "application/json");
		Check(res && res->status == 200 && IsWav(res->body), "ping gets a wav");
		std::scoped_lock lock{ last };
		Check(lastSpeak.text.empty(), "ping makes no line");
	}
	// what is not a line
	{
		const auto empty = client.Post("/tts_to_audio/", R"({"text":"","speaker_wav":"malenord"})", "application/json");
		Check(empty && empty->status == 400 && Detail(empty) == "Text is required", "no text: 400");
		const auto broken = client.Post("/tts_to_audio/", "{not json", "application/json");
		Check(broken && broken->status == 400, "a body that is not JSON: 400");
		const auto array = client.Post("/tts_to_audio/", "[1,2]", "application/json");
		Check(array && array->status == 400, "JSON that is not an object: 400");
	}
	// a line that is not ready keeps the plugin's status and reason
	{
		const auto res = client.Post("/tts_to_audio/", R"({"text":"Wait.","speaker_wav":"busy"})", "application/json");
		Check(res && res->status == 503 && Detail(res) == "not ready within 60 s", "busy: 503 with the reason");
	}

	// a voice sample, as SkyrimNet uploads it
	const auto sample = XttsHttp::Silence(16000, 2000);
	const auto upload = [&] {
		httplib::UploadFormDataItems items{
			{ "speaker_name", "FemaleNord", "", "" },
			{ "language", "en", "", "" },
			{ "wav_file", std::string(sample.begin(), sample.end()), "femalenord.wav", "audio/wav" },
		};
		return client.Post("/create_and_store_latents", items);
	};
	{
		const auto res = upload();
		Check(res && res->status == 404, "cloning off: 404, which SkyrimNet reads as no cloning");
		if (res) {
			Same(Detail(res), "voice cloning is off", "with the reason");
		}
		std::scoped_lock lock{ last };
		Same(lastClone.speaker, "femalenord", "the sample's speaker by name");
		Same(lastClone.language, "en", "its language");
		Same(lastClone.fileName, "femalenord.wav", "its file name");
		Check(lastClone.bytes == std::string(sample.begin(), sample.end()), "its bytes, whole");
	}
	cloning.store(true);
	{
		const auto res = upload();
		Check(res && res->status == 200, "cloning on: the sample is taken");
		if (res) {
			const auto body = nlohmann::json::parse(res->body, nullptr, false);
			Check(body.is_object() && body.value("speaker_name", "") == "femalenord", "and the answer names the speaker");
		}
	}
	{
		const auto res = client.Post("/create_and_store_latents", "speaker_name=x", "application/x-www-form-urlencoded");
		Check(res && res->status == 400, "a sample that is not a multipart form: 400");
	}

	{
		const auto res = client.Get("/health");
		Check(res && res->status == 200, "health");
		const auto res2 = client.Get("/speakers_list");
		Check(res2 && res2->status == 404, "what an XTTS server does not have: 404");
	}

	// another server on the same port is refused, the first goes on
	{
		XttsHttp::Server second;
		std::string      why;
		Check(!second.Start("127.0.0.1", PORT, handlers, why), "a port that is taken is refused");
		Check(!why.empty(), "with a reason");
		const auto res = client.Post("/tts_to_audio/", R"({"text":"Still here.","speaker_wav":"malenord"})", "application/json");
		Check(res && res->status == 200, "the first server still answers");
	}

	// stop, and the port can be had again
	server.Stop();
	Check(!server.Running(), "stopped");
	{
		httplib::Client gone("localhost", PORT);
		gone.set_connection_timeout(1);
		const auto res = gone.Get("/health");
		Check(!res, "nothing answers once stopped");
	}
	Check(server.Start("127.0.0.1", PORT, handlers, error), "it starts again on the same port: " + error);
	{
		httplib::Client again("localhost", PORT);
		const auto      res = again.Get("/health");
		Check(res && res->status == 200, "and answers");
	}
	server.Stop();

	std::cout << (g_failed ? "FAILED" : "OK") << ": " << g_failed << " failed\n";
	return g_failed ? 1 : 0;
}
