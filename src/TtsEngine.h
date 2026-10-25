#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct SherpaOnnxOfflineTts;

// Text-to-speech core: a thin wrapper over the statically linked sherpa-onnx
// runtime. No game dependencies, so tools\probe can drive it outside Skyrim.
namespace TtsEngine
{
	struct ModelSpec
	{
		// Model folder, handed to the runtime as given. Keep it relative to the
		// game root: the phonemizer opens its data with narrow-path calls, so an
		// absolute path through a non-ASCII install folder would not open.
		std::string dir;
		std::string family = "auto";  // auto | kokoro | vits (Piper voices are vits) | pocket
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

	// A recording a Pocket model speaks like: one voices\<name>.wav of the model
	// folder. The name (the file name without ".wav") is what a config refers to
	// it by; a voice number is its place in name order. The stamp (size and
	// write time) changes when the recording is replaced, and only then: a
	// recording added beside it leaves it as it is.
	struct Clip
	{
		std::string           name;
		std::filesystem::path path;
		std::uint64_t         stamp = 0;
	};

	// The recordings of a model folder, in name order (case ignored). Empty for
	// a folder that holds none, which every model but a Pocket one is.
	std::vector<Clip> Clips(const std::string& a_dir);

	// A recording to speak like, read as a Pocket model reads its own: any plain
	// wav (PCM of 8 to 32 bits or 32-bit float, any channel count), its first 10
	// seconds, at least half a second of it. false and a_error for anything else.
	bool ReadRecording(const std::filesystem::path& a_path, Audio& a_out, std::string& a_error);

	// Is a_dir a Pocket model's folder, the only kind that speaks like a
	// recording? Read off its files, as Model::Load's family "auto" does.
	bool IsPocketModel(const std::string& a_dir);

	// One loaded voice model. Several can be alive at once; each is used from
	// one thread at a time (the plugin's worker owns them all).
	//
	// A Pocket model (Kyutai's Pocket TTS) has no voices of its own: it speaks in
	// the voice of a short recording handed to it with each line, one of the
	// Clips of its folder. A voice number is a clip's place in name order (0 =
	// the first); VoiceIndex turns a clip's name into that. Only the first 10
	// seconds of a clip are used. Pocket takes no speed: a line's speed does not
	// change it.
	class Model
	{
	public:
		// nullptr and a_error when the folder is not a usable model. Checks the
		// folder itself first: the runtime answers some bad model files by ending
		// the process, so everything that can be verified up front is.
		static std::unique_ptr<Model> Load(const ModelSpec& a_spec, std::string& a_error);

		~Model();
		Model(const Model&) = delete;
		Model& operator=(const Model&) = delete;

		int                SampleRate() const { return _sampleRate; }
		int                VoiceCount() const { return _voices; }
		const std::string& Family() const { return _family; }  // kokoro, vits or pocket
		// Voice names by voice number: a Pocket model's clip names (file names
		// without ".wav"). Empty for the other families, whose voices are numbers.
		const std::vector<std::string>& VoiceNames() const { return _voiceNames; }
		// The voice number of a named recording (case ignored, ".wav" may be
		// given), -1 when the model has no such one. -1 for every name on a
		// model whose voices are numbers.
		int VoiceIndex(std::string_view a_name) const;

		// a_keepGoing is polled between sentences; returning false abandons the line.
		Result Synthesize(const std::string& a_text, int a_voice, float a_speed,
			const std::function<bool()>& a_keepGoing, Audio& a_out, std::string& a_error) const;

		// A Pocket model only: the line in the voice of a_clip, a recording that
		// is not one of the model's own (see ReadRecording). Fails on any other
		// model, which cannot speak like a recording.
		Result SynthesizeLike(const std::string& a_text, const Audio& a_clip, const std::function<bool()>& a_keepGoing,
			Audio& a_out, std::string& a_error) const;

	private:
		Model() = default;

		const SherpaOnnxOfflineTts*        _tts = nullptr;
		int                                _sampleRate = 0;
		int                                _voices = 0;
		std::string              _family;
		std::vector<Clip>        _clips;  // Pocket: voice N speaks like _clips[N]
		std::vector<std::string> _voiceNames;
		mutable int              _clipVoice = -1;  // the voice whose clip _clip holds
		mutable Audio            _clip;
	};

	std::string RuntimeVersion();

	// Cubic resample in place, meant for raising the rate (there is no low-pass
	// for lowering it). A no-op when a_targetRate <= 0 or already matches.
	void Resample(Audio& a_audio, int a_targetRate);

	// Text as the speech runtime wants it. The runtime takes UTF-8, and game
	// text is not always that: English and other Western-language data is
	// Windows-1252, where a curly apostrophe or an accented letter is one byte.
	// Text that is not valid UTF-8 is read as 1252. Typographic punctuation then
	// becomes the plain kind, which every model's pronunciation rules know.
	std::string ToSpeakable(std::string_view a_text);

	// FNV-1a over the bytes of a_text. Names cache files and seeds mouth shapes,
	// so it must not change between versions.
	std::uint64_t Hash64(std::string_view a_text);

	// A whole file, written in one go.
	bool WriteBytes(const std::filesystem::path& a_path, const void* a_data, std::size_t a_size, std::string& a_error);

	// Mono 16-bit PCM wav, written in one go.
	bool WriteWav(const std::filesystem::path& a_path, const Audio& a_audio, std::string& a_error);

	// The same file as bytes; empty when there is nothing to write.
	std::vector<char> EncodeWav(const Audio& a_audio);

	// Reads a wav this module wrote (mono 16-bit PCM, plain 44-byte header).
	// false for anything else, a truncated file included.
	bool ReadWav(const std::filesystem::path& a_path, Audio& a_out);

	// Size and write time of the model files in a_dir, as one number: it changes
	// when the model is replaced. 0 when the folder holds no model. A Pocket
	// model's recordings are not part of it (each Clip carries its own stamp),
	// so adding one leaves the lines of the others as they are.
	std::uint64_t ModelStamp(const std::string& a_dir);
}
