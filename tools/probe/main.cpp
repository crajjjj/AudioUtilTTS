// Offline check of the speech core, no game needed.
//   probe <model dir> "text" [voice] [out.wav] [threads] [output rate]
// Loads the model through the same TtsEngine the plugin uses, speaks one line
// and writes it as a wav, printing how long each step took. Beside the wav it
// writes the mouth animation the plugin would hand the game for that line
// (out.lip), built by the same Lip code; AudioUtil's LipSim plays the pair.
//   probe --rest out.lip
// Writes the closed-mouth placeholder alone.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "Lip.h"
#include "TtsEngine.h"

int main(int argc, char** argv)
{
	std::string error;
	if (argc == 3 && std::strcmp(argv[1], "--rest") == 0) {
		if (!Lip::Write(argv[2], Lip::Rest(), error)) {
			std::printf("write failed: %s\n", error.c_str());
			return 1;
		}
		std::printf("wrote %s\n", argv[2]);
		return 0;
	}
	if (argc < 3) {
		std::printf("usage: probe <model dir> \"text\" [voice] [out.wav] [threads] [output rate]\n"
					"       probe --rest out.lip\n");
		return 2;
	}
	TtsEngine::ModelSpec spec;
	spec.dir = argv[1];
	const std::string text = argv[2];
	const int         voice = argc > 3 ? std::atoi(argv[3]) : 0;
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
	std::printf("sherpa-onnx %s, %d voices, %d Hz, loaded in %lld ms, stamp %016llx\n",
		TtsEngine::RuntimeVersion().c_str(), model->VoiceCount(), model->SampleRate(), ms(t0, t1),
		static_cast<unsigned long long>(TtsEngine::ModelStamp(spec.dir)));

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
