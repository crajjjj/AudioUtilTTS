// Offline check of the speech core, no game needed.
//   probe <model dir> "text" [voice] [out.wav] [threads] [output rate]
// Loads the model through the same TtsEngine the plugin uses, speaks one line
// and writes it as a wav, printing how long each step took.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "TtsEngine.h"

int main(int argc, char** argv)
{
	if (argc < 3) {
		std::printf("usage: probe <model dir> \"text\" [voice] [out.wav] [threads] [output rate]\n");
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

	std::string error;
	const auto  t0 = std::chrono::steady_clock::now();
	if (!TtsEngine::Load(spec, error)) {
		std::printf("load failed: %s\n", error.c_str());
		return 1;
	}
	const auto t1 = std::chrono::steady_clock::now();
	std::printf("sherpa-onnx %s, %d voices, %d Hz, loaded in %lld ms\n", TtsEngine::RuntimeVersion().c_str(),
		TtsEngine::VoiceCount(), TtsEngine::SampleRate(), ms(t0, t1));

	TtsEngine::Audio audio;
	const auto       result = TtsEngine::Synthesize(text, voice, 1.0f, {}, audio, error);
	const auto       t2 = std::chrono::steady_clock::now();
	if (result != TtsEngine::Result::Ok) {
		std::printf("synthesis failed: %s\n", error.c_str());
		return 1;
	}
	std::printf("%.2f s of speech in %lld ms\n", static_cast<double>(audio.Seconds()), ms(t1, t2));

	TtsEngine::Resample(audio, outputRate);
	if (!TtsEngine::WriteWav(out, audio, error)) {
		std::printf("write failed: %s\n", error.c_str());
		return 1;
	}
	std::printf("wrote %s (%d Hz, %zu samples)\n", out.c_str(), audio.sampleRate, audio.samples.size());
	TtsEngine::Unload();
	return 0;
}
