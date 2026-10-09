#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Text-to-speech core: a thin wrapper over the statically linked sherpa-onnx
// runtime. No game dependencies, so tools\probe can drive it outside Skyrim.
//
// Threading: Load / Unload / Synthesize belong to ONE thread (the plugin's
// worker). The status getters read atomics and are safe from anywhere.
namespace TtsEngine
{
	struct ModelSpec
	{
		// Model folder, handed to the runtime as given. Keep it relative to the
		// game root: the phonemizer opens its data with narrow-path calls, so an
		// absolute path through a non-ASCII install folder would not open.
		std::string dir;
		std::string family = "auto";  // auto | kokoro | vits (Piper voices are vits)
		std::string lexicon;          // optional: comma-separated file names inside dir
		std::string lang;             // optional language hint (Kokoro)
		int         threads = 2;
	};

	struct Audio
	{
		std::vector<float> samples;  // mono, -1..1
		int                sampleRate = 0;

		float Seconds() const
		{
			return sampleRate > 0 ? static_cast<float>(samples.size()) / static_cast<float>(sampleRate) : 0.0f;
		}
	};

	enum class Result
	{
		Ok,
		Cancelled,
		Failed
	};

	// Replaces any model already loaded. Checks the folder itself first: the
	// runtime answers some bad model files by ending the process, so everything
	// that can be verified up front is.
	bool Load(const ModelSpec& a_spec, std::string& a_error);
	void Unload();

	bool        IsLoaded();
	int         SampleRate();
	int         VoiceCount();
	std::string RuntimeVersion();

	// a_keepGoing is polled between sentences; returning false abandons the line.
	Result Synthesize(const std::string& a_text, int a_voice, float a_speed,
		const std::function<bool()>& a_keepGoing, Audio& a_out, std::string& a_error);

	// Cubic resample in place, meant for raising the rate (there is no low-pass
	// for lowering it). A no-op when a_targetRate <= 0 or already matches.
	void Resample(Audio& a_audio, int a_targetRate);

	// Mono 16-bit PCM wav, written in one go.
	bool WriteWav(const std::filesystem::path& a_path, const Audio& a_audio, std::string& a_error);
}
