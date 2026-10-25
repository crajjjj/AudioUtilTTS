#include "XttsServer.h"

#include "Config.h"
#include "Speaker.h"
#include "TtsEngine.h"
#include "XttsHttp.h"

#include <Windows.h>

#include <thread>

namespace XttsServer
{
	namespace
	{
		namespace fs = std::filesystem;

		// loopback only: nothing outside this PC reaches it
		constexpr auto HOST = "127.0.0.1";
		// SkyrimNet waits for a line about this long before it gives up on it
		constexpr auto WAIT = std::chrono::seconds(60);
		constexpr auto SAMPLE_DIR = "Data\\SKSE\\Plugins\\AudioUtilTTS\\skyrimnet"sv;

		std::mutex           g_control;          // one Reconcile at a time
		XttsHttp::Server*    g_http = nullptr;   // never destroyed: lives as long as the process
		int                  g_port = 0;         // the port g_http listens on
		// set while the endpoint closes: requests still in the server's hands end
		// at once, so that closing does not wait for their lines
		std::atomic<bool>    g_closing{ false };
		std::mutex           g_sampleLock;       // one voice sample written at a time
		std::uint64_t        g_sampleCount = 0;  // names their temporary files; g_sampleLock
		std::mutex           g_stateLock;        // guards g_state and g_warned
		std::string          g_state = "not started";
		std::unordered_set<std::string> g_warned;  // see WarnOnce
		std::atomic<std::uint32_t> g_lines{ 0 };    // lines answered
		std::atomic<std::uint32_t> g_failed{ 0 };   // lines not
		std::atomic<std::uint32_t> g_samples{ 0 };  // voice samples kept

		void SetState(std::string a_state)
		{
			std::scoped_lock lock{ g_stateLock };
			g_state = std::move(a_state);
		}

		// A state that holds request after request goes into the log once.
		void LogOnce(const std::string& a_message, bool a_warn)
		{
			{
				std::scoped_lock lock{ g_stateLock };
				if (!g_warned.insert(a_message).second) {
					return;
				}
			}
			if (a_warn) {
				logger::warn("{}", a_message);
			} else {
				logger::info("{}", a_message);
			}
		}

		bool SkyrimNetLoaded() { return GetModuleHandleW(L"SkyrimNet.dll") != nullptr; }

		fs::path SampleFile(const std::string& a_speaker)
		{
			return fs::current_path() / SAMPLE_DIR / (a_speaker + ".wav");
		}

		// The model voice samples are spoken with is an installed Pocket model, as
		// far as its files tell. Looked at once per config: each line asks.
		bool CloneModelUsable(const std::shared_ptr<const Config::Settings>& a_settings)
		{
			static std::mutex                               lock;
			static std::shared_ptr<const Config::Settings> lookedAt;
			static bool                                     usable = false;
			std::scoped_lock                                hold{ lock };
			if (lookedAt != a_settings) {
				const auto dir = std::string(Config::MODELS_DIR) + Config::Normalize(a_settings->skyrimnetCloneModel);
				usable = TtsEngine::ModelStamp(dir) != 0 && TtsEngine::IsPocketModel(dir);
				lookedAt = a_settings;
			}
			return usable;
		}

		XttsHttp::SpeakResult Speak(const XttsHttp::SpeakRequest& a_request)
		{
			if (g_closing.load()) {
				return { 503, {}, "the server is closing" };
			}
			const auto settings = Config::Get();
			fs::path   clip;
			if (settings->skyrimnetCloning && !a_request.speaker.empty() && CloneModelUsable(settings)) {
				std::error_code ec;
				if (const auto file = SampleFile(a_request.speaker); fs::is_regular_file(file, ec)) {
					clip = file;
				}
			}
			const auto stillWanted = [&] {
				return !g_closing.load() && (!a_request.connected || a_request.connected());
			};
			auto line = Speaker::RenderLine(a_request.text, a_request.speaker, clip, WAIT, stillWanted);
			if (line.wav.empty()) {
				g_failed.fetch_add(1);
				logger::warn("SkyrimNet: no line for '{}' ({}): {}", a_request.speaker,
					line.voice.empty() ? "no voice"s : line.voice, line.error);
				return { line.later ? 503 : 500, {}, line.error };
			}
			g_lines.fetch_add(1);
			if (line.chars > 0) {
				logger::info("SkyrimNet: '{}' as {}: {} chars, {:.1f} s of speech in {} ms", a_request.speaker, line.voice,
					line.chars, line.seconds, line.ms);
			} else {
				logger::info("SkyrimNet: '{}': silence ({})", a_request.speaker, line.voice);
			}
			return { 200, std::move(line.wav), {} };
		}

		XttsHttp::CloneResult Clone(const XttsHttp::CloneRequest& a_request)
		{
			const auto settings = Config::Get();
			if (!settings->skyrimnetCloning) {
				// how an XTTS server says it takes no samples: SkyrimNet names its
				// speakers by voice type instead
				LogOnce("SkyrimNet sends voice samples: they are not kept, voice cloning is off ([skyrimnet] "
						"voice_cloning). SkyrimNet names its speakers by voice type instead.",
					false);
				return { 404, "voice cloning is off" };
			}
			if (!CloneModelUsable(settings)) {
				LogOnce(std::format("SkyrimNet: voice cloning is on, but '{}' ([skyrimnet] clone_model) is not an installed "
									"Pocket model: SkyrimNet's voice samples are not kept",
							settings->skyrimnetCloneModel),
					true);
				return { 404, "no Pocket model to clone with" };
			}
			if (a_request.speaker.empty()) {
				return { 400, "speaker_name is required" };
			}
			if (a_request.bytes.empty()) {
				return { 400, "wav_file is required" };
			}
			// Read the way it will be used, and kept as only what is used: the
			// first 10 seconds, mono 16-bit, so a long or odd upload costs no
			// disk and a sample that cannot be spoken like is never kept. One at
			// a time: SkyrimNet may send a speaker's sample twice at once.
			std::scoped_lock lock{ g_sampleLock };
			const auto       file = SampleFile(a_request.speaker);
			auto             upload = file;
			upload += std::format(".{}.upload", ++g_sampleCount);
			std::error_code ec;
			fs::create_directories(file.parent_path(), ec);
			std::string error;
			if (!TtsEngine::WriteBytes(upload, a_request.bytes.data(), a_request.bytes.size(), error)) {
				logger::warn("SkyrimNet: the voice sample for '{}' could not be written: {}", a_request.speaker, error);
				return { 500, error };
			}
			TtsEngine::Audio sample;
			const bool       read = TtsEngine::ReadRecording(upload, sample, error);
			fs::remove(upload, ec);
			if (!read) {
				logger::warn("SkyrimNet: the voice sample for '{}' is not kept: {}", a_request.speaker, error);
				return { 400, "Only WAV files are supported: " + error };
			}
			auto part = file;
			part += ".part";
			if (!TtsEngine::WriteWav(part, sample, error)) {
				logger::warn("SkyrimNet: the voice sample for '{}' could not be written: {}", a_request.speaker, error);
				return { 500, error };
			}
			// the worker may be reading the old one this instant: a moment later
			// it is not
			for (int attempt = 0;; ++attempt) {
				fs::rename(part, file, ec);
				if (!ec || attempt == 4) {
					break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			if (ec) {
				fs::remove(part, ec);
				return { 500, "the voice sample could not be kept" };
			}
			g_samples.fetch_add(1);
			logger::info("SkyrimNet: voice sample for '{}' kept ({:.1f} s of it used)", a_request.speaker, sample.Seconds());
			return { 200, {} };
		}

		// Brings the endpoint in line with the config. g_control held.
		void Reconcile()
		{
			const auto settings = Config::Get();
			const bool installed = SkyrimNetLoaded();
			const bool wanted = settings->skyrimnet && installed;
			const int  port = settings->skyrimnetPort;
			if (g_http && g_http->Running() && (!wanted || port != g_port)) {
				// Requests waiting for their line are answered at once, and so is
				// every one the server still takes before it stops (they see
				// g_closing), so the server's threads end and Stop waits for none.
				g_closing.store(true);
				Speaker::DropRenderedLines();
				g_http->Stop();
				g_closing.store(false);
				logger::info("SkyrimNet: no longer answered at http://localhost:{}", g_port);
			}
			if (!wanted) {
				SetState(!settings->skyrimnet ? "off ([skyrimnet] enable)" : "not installed");
				return;
			}
			if (g_http && g_http->Running()) {
				return;
			}
			if (!g_http) {
				g_http = new XttsHttp::Server();
			}
			XttsHttp::Handlers handlers;
			handlers.speak = Speak;
			handlers.clone = Clone;
			handlers.log = [](bool a_warn, const std::string& a_text) {
				if (a_warn) {
					logger::warn("{}", a_text);
				} else {
					logger::info("{}", a_text);
				}
			};
			std::string error;
			if (g_http->Start(HOST, port, std::move(handlers), error)) {
				g_port = port;
				SetState(std::format("answered at http://localhost:{}", port));
				logger::info("SkyrimNet: its lines are answered at http://localhost:{}, SkyrimNet's XTTS address: choose "
							 "XTTS as its voice engine",
					port);
			} else {
				SetState(std::format("port {} is taken by another program", port));
				logger::warn("SkyrimNet: port {} could not be opened ({}). SkyrimNet goes on using whatever answers there; "
							 "to use this plugin's voices, close that program or give both a free port ([skyrimnet] port)",
					port, error);
			}
		}
	}

	void Apply()
	{
		std::thread([] {
			try {
				std::scoped_lock lock{ g_control };
				Reconcile();
			} catch (const std::exception& e) {
				logger::error("SkyrimNet: the endpoint could not be set up: {}", e.what());
			} catch (...) {
				logger::error("SkyrimNet: the endpoint could not be set up");
			}
		}).detach();
	}

	std::string Status()
	{
		std::string state;
		{
			std::scoped_lock lock{ g_stateLock };
			state = g_state;
		}
		const auto lines = g_lines.load();
		const auto failed = g_failed.load();
		const auto samples = g_samples.load();
		if (lines == 0 && failed == 0 && samples == 0) {
			return state;
		}
		return std::format("{}: {} lines{}{}", state, lines, failed ? std::format(", {} not made", failed) : ""s,
			samples ? std::format(", {} voice samples kept", samples) : ""s);
	}
}
