// Offline check of the speech core, no game needed.
//   probe <model dir> "text" [voice] [out.wav] [threads] [output rate]
// voice is a number, or on a Pocket model the name of one of its recordings.
// Loads the model through the same TtsEngine the plugin uses, speaks one line
// and writes it as a wav, printing how long each step took. Beside the wav it
// writes the mouth animation the plugin would hand the game for that line
// (out.lip), built by the same Lip code; AudioUtil's LipSim plays the pair.
//   probe --rest out.lip
// Writes the closed-mouth placeholder alone.
//   probe --text file
// Reads the file's bytes as a line of game text and prints, in hex and as
// text, what the speech runtime would be handed (TtsEngine::ToSpeakable).
//   probe --batch <model dir> <list file> <out dir> [voice] [threads] [output rate] [lip strength]
// Speaks a whole list with the model loaded once. The list is UTF-8, one line
// each: a file name, a tab, the text. Writes <out dir>\<file name>.wav and
// .lip. A line whose wav is already there is skipped, so a stopped run resumes.
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "Lip.h"
#include "TtsEngine.h"

namespace
{
	// a Pocket model's voices are its clips: which number is which
	void PrintVoiceNames(const TtsEngine::Model& a_model)
	{
		const auto& names = a_model.VoiceNames();
		for (std::size_t i = 0; i < names.size(); ++i) {
			std::printf("  voice %zu: %s\n", i, names[i].c_str());
		}
	}

	// the voice argument: a number, or on a Pocket model a recording's name; -1 = no such recording
	int VoiceArg(const TtsEngine::Model& a_model, const char* a_arg)
	{
		if (!a_arg || !*a_arg) {
			return 0;
		}
		if (std::isdigit(static_cast<unsigned char>(a_arg[0]))) {
			return std::atoi(a_arg);
		}
		const int voice = a_model.VoiceIndex(a_arg);
		if (voice < 0) {
			std::printf("no recording '%s' in the model (voices\\%s.wav)\n", a_arg, a_arg);
		}
		return voice;
	}

	int Batch(int argc, char** argv)
	{
		namespace fs = std::filesystem;
		using clock = std::chrono::steady_clock;

		TtsEngine::ModelSpec spec;
		spec.dir = argv[2];
		const fs::path outDir = argv[4];
		const char*    voiceArg = argc > 5 ? argv[5] : "0";
		spec.threads = argc > 6 ? std::atoi(argv[6]) : 2;
		const int   outputRate = argc > 7 ? std::atoi(argv[7]) : 0;
		const float strength = argc > 8 ? static_cast<float>(std::atof(argv[8])) : 0.8f;

		std::ifstream list(argv[3], std::ios::binary);
		if (!list) {
			std::printf("cannot read the list: %s\n", argv[3]);
			return 1;
		}
		std::error_code ec;
		fs::create_directories(outDir, ec);

		std::string error;
		const auto  model = TtsEngine::Model::Load(spec, error);
		if (!model) {
			std::printf("load failed: %s\n", error.c_str());
			return 1;
		}
		const int voice = VoiceArg(*model, voiceArg);
		if (voice < 0) {
			return 1;
		}
		std::printf("sherpa-onnx %s, %s, %d voices, %d Hz, voice %d, %d threads\n", TtsEngine::RuntimeVersion().c_str(),
			model->Family().c_str(), model->VoiceCount(), model->SampleRate(), voice, spec.threads);
		PrintVoiceNames(*model);

		const auto  start = clock::now();
		int         spoken = 0, skipped = 0, failed = 0;
		double      seconds = 0.0;
		std::string line;
		while (std::getline(list, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			const auto tab = line.find('\t');
			if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) {
				continue;
			}
			// the list is UTF-8; a narrow string would be read in the ANSI code page
			const auto        name = std::u8string(line.begin(), line.begin() + static_cast<std::ptrdiff_t>(tab));
			const std::string text = line.substr(tab + 1);
			auto              wav = outDir / fs::path(name);
			wav += ".wav";
			if (fs::exists(wav, ec)) {
				++skipped;
				continue;
			}

			TtsEngine::Audio audio;
			if (model->Synthesize(TtsEngine::ToSpeakable(text), voice, 1.0f, {}, audio, error) != TtsEngine::Result::Ok ||
				audio.samples.empty()) {
				std::printf("failed: %s (%s)\n", line.substr(0, tab).c_str(), error.c_str());
				++failed;
				continue;
			}
			TtsEngine::Resample(audio, outputRate);
			auto lipPath = wav;
			lipPath.replace_extension(".lip");
			// the wav goes last and under its final name only when whole: it is what marks a line as done
			auto part = wav;
			part += ".part";
			if (!Lip::Write(lipPath, Lip::FromAudio(audio, text, strength), error) ||
				!TtsEngine::WriteWav(part, audio, error)) {
				std::printf("write failed: %s (%s)\n", line.substr(0, tab).c_str(), error.c_str());
				++failed;
				continue;
			}
			fs::rename(part, wav, ec);
			seconds += static_cast<double>(audio.Seconds());
			if (++spoken % 100 == 0) {
				const auto took = std::chrono::duration<double>(clock::now() - start).count();
				std::printf("%d lines, %.0f s of speech in %.0f s\n", spoken, seconds, took);
				std::fflush(stdout);
			}
		}
		const auto took = std::chrono::duration<double>(clock::now() - start).count();
		std::printf("done: %d spoken (%.0f s of speech in %.0f s), %d already there, %d failed\n", spoken, seconds, took,
			skipped, failed);
		return failed > 0 && spoken == 0 ? 1 : 0;
	}
}

int main(int argc, char** argv)
{
	std::string error;
	if (argc >= 5 && std::strcmp(argv[1], "--batch") == 0) {
		return Batch(argc, argv);
	}
	if (argc == 3 && std::strcmp(argv[1], "--rest") == 0) {
		if (!Lip::Write(argv[2], Lip::Rest(), error)) {
			std::printf("write failed: %s\n", error.c_str());
			return 1;
		}
		std::printf("wrote %s\n", argv[2]);
		return 0;
	}
	if (argc == 3 && std::strcmp(argv[1], "--text") == 0) {
		std::ifstream     in(argv[2], std::ios::binary);
		const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		const auto        speakable = TtsEngine::ToSpeakable(raw);
		for (const char c : speakable) {
			std::printf("%02X ", static_cast<unsigned char>(c));
		}
		std::printf("\n%s\n", speakable.c_str());
		return 0;
	}
	if (argc < 3) {
		std::printf("usage: probe <model dir> \"text\" [voice] [out.wav] [threads] [output rate]\n"
					"       probe --rest out.lip\n"
					"       probe --text file\n"
					"       probe --batch <model dir> <list file> <out dir> [voice] [threads] [output rate] [lip strength]\n");
		return 2;
	}
	TtsEngine::ModelSpec spec;
	spec.dir = argv[1];
	const std::string text = argv[2];
	const char*       voiceArg = argc > 3 ? argv[3] : "0";
	const std::string out = argc > 4 ? argv[4] : "probe.wav";
	spec.threads = argc > 5 ? std::atoi(argv[5]) : 2;
	const int outputRate = argc > 6 ? std::atoi(argv[6]) : 0;

	const auto ms = [](auto a_from, auto a_to) {
		return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(a_to - a_from).count());
	};

	const auto t0 = std::chrono::steady_clock::now();
	const auto model = TtsEngine::Model::Load(spec, error);
	if (!model) {
		std::printf("load failed: %s\n", error.c_str());
		return 1;
	}
	const auto t1 = std::chrono::steady_clock::now();
	std::printf("sherpa-onnx %s, %s, %d voices, %d Hz, loaded in %lld ms, stamp %016llx\n",
		TtsEngine::RuntimeVersion().c_str(), model->Family().c_str(), model->VoiceCount(), model->SampleRate(), ms(t0, t1),
		static_cast<unsigned long long>(TtsEngine::ModelStamp(spec.dir)));
	PrintVoiceNames(*model);
	const int voice = VoiceArg(*model, voiceArg);
	if (voice < 0) {
		return 1;
	}

	TtsEngine::Audio audio;
	const auto       result = model->Synthesize(text, voice, 1.0f, {}, audio, error);
	const auto       t2 = std::chrono::steady_clock::now();
	if (result != TtsEngine::Result::Ok) {
		std::printf("synthesis failed: %s\n", error.c_str());
		return 1;
	}
	std::printf("%.2f s of speech in %lld ms (%.1f ms per character)\n", static_cast<double>(audio.Seconds()),
		ms(t1, t2), text.empty() ? 0.0 : static_cast<double>(ms(t1, t2)) / static_cast<double>(text.size()));

	TtsEngine::Resample(audio, outputRate);
	if (!TtsEngine::WriteWav(out, audio, error)) {
		std::printf("write failed: %s\n", error.c_str());
		return 1;
	}
	std::printf("wrote %s (%d Hz, %zu samples)\n", out.c_str(), audio.sampleRate, audio.samples.size());

	// the round trip the plugin's cache makes
	TtsEngine::Audio back;
	if (!TtsEngine::ReadWav(out, back) || back.samples.size() != audio.samples.size()) {
		std::printf("read back failed\n");
		return 1;
	}

	const auto lipPath = std::filesystem::path(out).replace_extension(".lip");
	const auto t3 = std::chrono::steady_clock::now();
	const auto lip = Lip::FromAudio(back, text, 0.8f);
	const auto t4 = std::chrono::steady_clock::now();
	if (!Lip::Write(lipPath, lip, error)) {
		std::printf("lip write failed: %s\n", error.c_str());
		return 1;
	}
	std::printf("wrote %s (%zu bytes, built in %lld ms)\n", lipPath.string().c_str(), lip.size(), ms(t3, t4));
	return 0;
}
