#include "XttsHttp.h"

// httplib calls WSAStartup from a static initializer, so while the DLL loads,
// where Microsoft warns it may deadlock by loading Winsock's helpers. Not here:
// SkyrimSE.exe imports ws2_32.dll itself, so Winsock is up before any SKSE
// plugin loads and the call only counts a reference.
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <thread>

namespace XttsHttp
{
	namespace
	{
		using Json = nlohmann::json;

		// What a request may carry: a line of text, a voice sample of a few
		// seconds. Far below this either way.
		constexpr std::size_t MAX_BODY = 32u * 1024u * 1024u;
		// Requests answered at once. Lines are made one at a time anyway; the
		// rest wait their turn inside the plugin, not in the socket.
		constexpr std::size_t THREADS = 4;

		std::string Lower(std::string a_text)
		{
			std::transform(a_text.begin(), a_text.end(), a_text.begin(),
				[](unsigned char c) { return static_cast<char>(c < 0x80 ? std::tolower(c) : c); });
			return a_text;
		}

		// A string field of a JSON object, "" when it is missing or not a string.
		std::string Field(const Json& a_body, const char* a_key)
		{
			const auto it = a_body.find(a_key);
			return it != a_body.end() && it->is_string() ? it->get<std::string>() : std::string{};
		}

		// The error body FastAPI servers send, which XTTS clients read
		void Fail(httplib::Response& a_res, int a_status, const std::string& a_detail)
		{
			a_res.status = a_status;
			a_res.set_content(Json{ { "detail", a_detail } }.dump(), "application/json");
		}

		void Put16(std::vector<char>& a_out, std::uint16_t a_v)
		{
			a_out.push_back(static_cast<char>(a_v & 0xFF));
			a_out.push_back(static_cast<char>(a_v >> 8));
		}

		void Put32(std::vector<char>& a_out, std::uint32_t a_v)
		{
			for (int i = 0; i < 4; ++i) {
				a_out.push_back(static_cast<char>((a_v >> (8 * i)) & 0xFF));
			}
		}
	}

	std::string SpeakerName(std::string_view a_speakerWav)
	{
		auto name = a_speakerWav;
		if (const auto slash = name.find_last_of("/\\"); slash != std::string_view::npos) {
			name.remove_prefix(slash + 1);
		}
		auto lower = Lower(std::string(name));
		for (const auto* ext : { ".wav", ".json", ".pt" }) {
			const auto len = std::strlen(ext);
			if (lower.size() > len && lower.ends_with(ext)) {
				lower.resize(lower.size() - len);
				break;
			}
		}
		std::string out;
		for (const char c : lower) {
			const auto u = static_cast<unsigned char>(c);
			if (u < 0x80 && (std::isalnum(u) != 0 || c == '_' || c == '-' || c == '.')) {
				out += c;
			}
		}
		// never a name a path could step out of the folder with
		while (!out.empty() && out.front() == '.') {
			out.erase(out.begin());
		}
		if (out.size() > 64) {
			out.resize(64);
		}
		return out;
	}

	std::vector<char> Silence(int a_rate, int a_ms)
	{
		const auto        frames = static_cast<std::uint32_t>(std::max(a_rate, 1) / 1000 * std::max(a_ms, 0));
		const auto        bytes = frames * 2;
		std::vector<char> out;
		out.reserve(44 + bytes);
		out.insert(out.end(), { 'R', 'I', 'F', 'F' });
		Put32(out, 36 + bytes);
		out.insert(out.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
		Put32(out, 16);
		Put16(out, 1);  // PCM
		Put16(out, 1);  // mono
		Put32(out, static_cast<std::uint32_t>(a_rate));
		Put32(out, static_cast<std::uint32_t>(a_rate) * 2);
		Put16(out, 2);
		Put16(out, 16);
		out.insert(out.end(), { 'd', 'a', 't', 'a' });
		Put32(out, bytes);
		out.resize(out.size() + bytes, 0);
		return out;
	}

	struct Server::Impl
	{
		std::unique_ptr<httplib::Server> http;
		std::thread                      thread;
	};

	Server::Server() :
		_impl(std::make_unique<Impl>())
	{}

	Server::~Server() { Stop(); }

	bool Server::Running() const { return _impl->http != nullptr; }

	bool Server::Start(const std::string& a_host, int a_port, Handlers a_handlers, std::string& a_error)
	{
		if (_impl->http) {
			a_error = "already running";
			return false;
		}
		auto http = std::make_unique<httplib::Server>();
		http->new_task_queue = [] { return new httplib::ThreadPool(THREADS); };
		http->set_payload_max_length(MAX_BODY);
		// httplib's own default, SO_REUSEADDR, lets a socket on Windows take a port
		// another program is listening on, and a real XTTS server would lose its
		// requests to this one. Exclusive: a port that is taken is refused, and
		// this one cannot be taken either.
		http->set_socket_options([](SOCKET a_sock) {
			const BOOL on = TRUE;
			setsockopt(a_sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&on), sizeof(on));
		});

		const auto log = [handlers = a_handlers](bool a_warn, const std::string& a_text) {
			if (handlers.log) {
				handlers.log(a_warn, a_text);
			}
		};

		const auto speak = [handlers = a_handlers, log](const httplib::Request& a_req, httplib::Response& a_res) {
			const auto body = Json::parse(a_req.body, nullptr, false);
			if (body.is_discarded() || !body.is_object()) {
				return Fail(a_res, 400, "the body is not a JSON object");
			}
			SpeakRequest request;
			request.text = Field(body, "text");
			request.speaker = SpeakerName(Field(body, "speaker_wav"));
			request.language = Field(body, "language");
			if (request.text.empty()) {
				return Fail(a_res, 400, "Text is required");
			}
			if (request.text == "ping") {
				const auto silence = Silence(24000, 100);
				a_res.set_content(silence.data(), silence.size(), "audio/wav");
				return;
			}
			if (!handlers.speak) {
				return Fail(a_res, 500, "Model not loaded");
			}
			SpeakResult result;
			try {
				result = handlers.speak(request);
			} catch (const std::exception& e) {
				log(true, std::string("SkyrimNet: a line failed: ") + e.what());
				return Fail(a_res, 500, "Internal server error");
			}
			if (result.status != 200 || result.wav.empty()) {
				return Fail(a_res, result.status != 200 ? result.status : 500,
					result.error.empty() ? "no audio" : result.error);
			}
			a_res.set_content(result.wav.data(), result.wav.size(), "audio/wav");
		};
		http->Post("/tts_to_audio/", speak);
		http->Post("/tts_to_audio", speak);

		http->Post("/create_and_store_latents", [handlers = a_handlers, log](const httplib::Request& a_req, httplib::Response& a_res) {
			if (!a_req.is_multipart_form_data()) {
				return Fail(a_res, 400, "a multipart form is expected");
			}
			CloneRequest request;
			request.speaker = SpeakerName(a_req.form.get_field("speaker_name"));
			request.language = a_req.form.get_field("language");
			if (a_req.form.has_file("wav_file")) {
				auto file = a_req.form.get_file("wav_file");
				request.fileName = std::move(file.filename);
				request.bytes = std::move(file.content);
			}
			if (!handlers.clone) {
				return Fail(a_res, 404, "this server takes no voice samples");
			}
			CloneResult result;
			try {
				result = handlers.clone(request);
			} catch (const std::exception& e) {
				log(true, std::string("SkyrimNet: a voice sample failed: ") + e.what());
				return Fail(a_res, 500, "Internal server error");
			}
			if (result.status != 200) {
				return Fail(a_res, result.status, result.error);
			}
			const Json answer{ { "message", "Latents created and stored for speaker '" + request.speaker + "' in language '" +
												request.language + "'" },
				{ "speaker_name", request.speaker }, { "language", request.language } };
			a_res.set_content(answer.dump(), "application/json");
		});

		http->Get("/health", [](const httplib::Request&, httplib::Response& a_res) {
			a_res.set_content(Json{ { "status", "healthy" }, { "model_loaded", true } }.dump(), "application/json");
		});

		if (!http->bind_to_port(a_host, a_port)) {
			a_error = "the port is taken: another program, likely another XTTS server, listens on it";
			return false;
		}
		auto* raw = http.get();
		_impl->http = std::move(http);
		_impl->thread = std::thread([raw] { raw->listen_after_bind(); });
		// a stop that came before the thread is listening would be missed, and
		// Stop would wait for it forever
		raw->wait_until_ready();
		return true;
	}

	void Server::Stop()
	{
		if (!_impl->http) {
			return;
		}
		_impl->http->stop();
		if (_impl->thread.joinable()) {
			_impl->thread.join();
		}
		_impl->http.reset();
	}
}
