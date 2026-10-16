#include "TtsEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <system_error>
#include <utility>

#include <Windows.h>

#include <sherpa-onnx/c-api/c-api.h>

namespace TtsEngine
{
	namespace
	{
		namespace fs = std::filesystem;

		// config text is UTF-8; fs::path would read a narrow string in the ANSI code page
		fs::path U8(const std::string& a_text)
		{
			return fs::path(std::u8string(a_text.begin(), a_text.end()));
		}

		std::string Narrow(const fs::path& a_path)
		{
			const auto u8 = a_path.u8string();
			return std::string(u8.begin(), u8.end());
		}

		std::string Join(const std::string& a_dir, const std::string& a_name)
		{
			if (a_dir.empty()) {
				return a_name;
			}
			const char last = a_dir.back();
			return (last == '\\' || last == '/') ? a_dir + a_name : a_dir + "\\" + a_name;
		}

		// "model.onnx" when present, else the first *.onnx by name
		std::string FindModel(const fs::path& a_dir)
		{
			std::error_code ec;
			if (fs::is_regular_file(a_dir / "model.onnx", ec)) {
				return "model.onnx";
			}
			std::vector<std::string> found;
			for (const auto& entry : fs::directory_iterator(a_dir, ec)) {
				if (entry.is_regular_file(ec) && entry.path().extension() == ".onnx") {
					found.push_back(Narrow(entry.path().filename()));
				}
			}
			std::sort(found.begin(), found.end());
			return found.empty() ? std::string{} : found.front();
		}

		// "a.txt, b.txt" -> "<dir>\a.txt,<dir>\b.txt"; "" when a listed file is missing
		std::string LexiconPaths(const ModelSpec& a_spec, std::string& a_missing)
		{
			std::string out;
			std::size_t start = 0;
			while (start <= a_spec.lexicon.size()) {
				auto end = a_spec.lexicon.find(',', start);
				if (end == std::string::npos) {
					end = a_spec.lexicon.size();
				}
				auto name = a_spec.lexicon.substr(start, end - start);
				name.erase(0, name.find_first_not_of(" \t"));
				name.erase(name.find_last_not_of(" \t") + 1);
				if (!name.empty()) {
					std::error_code ec;
					if (!fs::is_regular_file(U8(a_spec.dir) / U8(name), ec)) {
						a_missing = name;
						return {};
					}
					if (!out.empty()) {
						out += ',';
					}
					out += Join(a_spec.dir, name);
				}
				start = end + 1;
			}
			return out;
		}

		bool IsUtf8(std::string_view a_text)
		{
			for (std::size_t i = 0; i < a_text.size();) {
				const auto  c = static_cast<unsigned char>(a_text[i]);
				std::size_t extra = 0;
				if (c >= 0x80) {
					if (c >= 0xC2 && c <= 0xDF) {
						extra = 1;
					} else if (c >= 0xE0 && c <= 0xEF) {
						extra = 2;
					} else if (c >= 0xF0 && c <= 0xF4) {
						extra = 3;
					} else {
						return false;
					}
					if (i + extra >= a_text.size()) {
						return false;
					}
					for (std::size_t k = 1; k <= extra; ++k) {
						if ((static_cast<unsigned char>(a_text[i + k]) & 0xC0) != 0x80) {
							return false;
						}
					}
				}
				i += extra + 1;
			}
			return true;
		}

		int32_t OnProgress(const float*, int32_t, float, void* a_arg)
		{
			const auto* keepGoing = static_cast<const std::function<bool()>*>(a_arg);
			return (!*keepGoing || (*keepGoing)()) ? 1 : 0;
		}

		// ------------------------------------------------------------------ Pocket

		// Pocket uses the first 10 seconds of a clip (the runtime's own limit, so
		// reading more would only cost memory).
		constexpr float CLIP_SECONDS = 10.0f;
		constexpr float CLIP_MIN_SECONDS = 0.5f;

		std::string Lower(std::string a_text)
		{
			std::transform(a_text.begin(), a_text.end(), a_text.begin(),
				[](unsigned char c) { return static_cast<char>(c < 0x80 ? std::tolower(c) : c); });
			return a_text;
		}

		// The files of a_dir whose lowercase name passes a_take, by lowercase name.
		// Stepped by hand: a range-for steps with the throwing overload.
		template <class Take>
		std::vector<fs::path> FilesIn(const fs::path& a_dir, Take a_take)
		{
			std::vector<std::pair<std::string, fs::path>> found;
			std::error_code                               ec;
			for (fs::directory_iterator it(a_dir, ec), end; !ec && it != end; it.increment(ec)) {
				std::error_code fileEc;
				if (!it->is_regular_file(fileEc)) {
					continue;
				}
				auto name = Lower(Narrow(it->path().filename()));
				if (a_take(name)) {
					found.emplace_back(std::move(name), it->path());
				}
			}
			std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
			std::vector<fs::path> paths;
			for (auto& [name, path] : found) {
				paths.push_back(std::move(path));
			}
			return paths;
		}

		// One part of a Pocket model, "<stem>*.onnx": "<stem>.int8.onnx" when
		// there, else "<stem>.onnx", else the first such file by name. "" = none.
		std::string FindPart(const fs::path& a_dir, const std::string& a_stem)
		{
			const auto parts = FilesIn(a_dir, [&](const std::string& a_name) {
				return a_name.starts_with(a_stem) && a_name.ends_with(".onnx");
			});
			if (parts.empty()) {
				return {};
			}
			for (const auto* wanted : { ".int8.onnx", ".onnx" }) {
				for (const auto& part : parts) {
					if (Lower(Narrow(part.filename())) == a_stem + wanted) {
						return Narrow(part.filename());
					}
				}
			}
			return Narrow(parts.front().filename());
		}

		std::vector<fs::path> ClipsIn(const fs::path& a_dir)
		{
			return FilesIn(a_dir / "voices", [](const std::string& a_name) { return a_name.ends_with(".wav"); });
		}

		// A recording to speak like: any plain wav, PCM of 8 to 32 bits or 32-bit
		// float, with any number of channels (mixed to one). The first a_seconds
		// only. Unlike ReadWav, it reads files people made, not files this
		// module wrote: extra chunks and odd formats are expected.
		bool ReadClip(const fs::path& a_path, float a_seconds, Audio& a_out, std::string& a_error)
		{
			a_out = {};
			std::ifstream in(a_path, std::ios::binary);
			if (!in) {
				a_error = "cannot be opened";
				return false;
			}
			char riff[12];
			if (!in.read(riff, sizeof(riff)) || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) {
				a_error = "not a wav file";
				return false;
			}
			std::uint16_t format = 0, channels = 0, block = 0, bits = 0;
			std::uint32_t rate = 0;
			bool          hasFormat = false;
			for (;;) {
				char head[8];
				if (!in.read(head, sizeof(head))) {
					a_error = "no audio in it";
					return false;
				}
				std::uint32_t size;
				std::memcpy(&size, head + 4, 4);
				if (std::memcmp(head, "fmt ", 4) == 0) {
					char fmt[64]{};
					if (size < 16 || size > sizeof(fmt) || !in.read(fmt, size)) {
						a_error = "its format block is unreadable";
						return false;
					}
					std::memcpy(&format, fmt, 2);
					std::memcpy(&channels, fmt + 2, 2);
					std::memcpy(&rate, fmt + 4, 4);
					std::memcpy(&block, fmt + 12, 2);
					std::memcpy(&bits, fmt + 14, 2);
					if (format == 0xFFFE && size >= 26) {
						std::memcpy(&format, fmt + 24, 2);  // extensible: the sub-format starts with the plain code
					}
					hasFormat = true;
					in.ignore(size & 1);
					continue;
				}
				if (std::memcmp(head, "data", 4) != 0) {
					in.ignore(static_cast<std::streamsize>(size) + (size & 1));
					continue;
				}
				if (!hasFormat) {
					a_error = "its audio comes before its format";
					return false;
				}
				const bool pcm = format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
				const bool real = format == 3 && bits == 32;
				if (!pcm && !real) {
					a_error = "only PCM and 32-bit float wavs are read (this one: format " + std::to_string(format) + ", " +
					          std::to_string(bits) + " bits)";
					return false;
				}
				if (channels == 0 || channels > 8 || rate < 8000 || rate > 192000 || block != channels * (bits / 8)) {
					a_error = "its format block makes no sense";
					return false;
				}
				const auto wanted = static_cast<std::uint64_t>(a_seconds * static_cast<float>(rate));
				const auto frames = std::min<std::uint64_t>(size / block, wanted);
				std::vector<char> bytes(static_cast<std::size_t>(frames * block));
				in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
				const auto got = static_cast<std::size_t>(in.gcount()) / block;
				const auto bytesPer = static_cast<std::size_t>(bits / 8);
				const auto sample = [&](const char* a_at) -> float {
					if (real) {
						float v;
						std::memcpy(&v, a_at, 4);
						return std::isfinite(v) ? v : 0.0f;
					}
					switch (bits) {
					case 8:
						return (static_cast<float>(static_cast<std::uint8_t>(*a_at)) - 128.0f) / 128.0f;
					case 16:
						{
							std::int16_t v;
							std::memcpy(&v, a_at, 2);
							return static_cast<float>(v) / 32768.0f;
						}
					case 24:
						{
							const auto* b = reinterpret_cast<const std::uint8_t*>(a_at);
							auto        v = static_cast<std::int32_t>(b[0] | (b[1] << 8) | (b[2] << 16));
							if (v & 0x800000) {
								v -= 0x1000000;
							}
							return static_cast<float>(v) / 8388608.0f;
						}
					default:
						{
							std::int32_t v;
							std::memcpy(&v, a_at, 4);
							return static_cast<float>(static_cast<double>(v) / 2147483648.0);
						}
					}
				};
				a_out.samples.resize(got);
				for (std::size_t i = 0; i < got; ++i) {
					const char* frame = bytes.data() + i * block;
					float       sum = 0.0f;
					for (std::size_t c = 0; c < channels; ++c) {
						sum += sample(frame + c * bytesPer);
					}
					a_out.samples[i] = std::clamp(sum / static_cast<float>(channels), -1.0f, 1.0f);
				}
				a_out.sampleRate = static_cast<int>(rate);
				if (a_out.Seconds() < CLIP_MIN_SECONDS) {
					a_error = "too short: a voice is copied from about 5 to 10 seconds of speech";
					a_out = {};
					return false;
				}
				return true;
			}
		}

		struct PocketFiles
		{
			std::string           lmFlow, lmMain, encoder, decoder, textConditioner, vocab, scores;
			std::vector<fs::path> clips;
			std::vector<std::string> names;
		};

		bool FindPocketFiles(const ModelSpec& a_spec, const fs::path& a_dir, PocketFiles& a_out, std::string& a_error)
		{
			const std::pair<std::string*, const char*> parts[] = {
				{ &a_out.lmFlow, "lm_flow" },
				{ &a_out.lmMain, "lm_main" },
				{ &a_out.encoder, "encoder" },
				{ &a_out.decoder, "decoder" },
				{ &a_out.textConditioner, "text_conditioner" },
			};
			for (const auto& [path, stem] : parts) {
				const auto name = FindPart(a_dir, stem);
				if (name.empty()) {
					a_error = std::string(stem) + "*.onnx missing in " + a_spec.dir + " (Pocket model)";
					return false;
				}
				*path = Join(a_spec.dir, name);
			}
			std::error_code ec;
			for (const auto& [path, name] : { std::pair{ &a_out.vocab, "vocab.json" }, std::pair{ &a_out.scores, "token_scores.json" } }) {
				if (!fs::is_regular_file(a_dir / name, ec)) {
					a_error = std::string(name) + " missing in " + a_spec.dir + " (Pocket model)";
					return false;
				}
				*path = Join(a_spec.dir, name);
			}
			a_out.clips = ClipsIn(a_dir);
			if (a_out.clips.empty()) {
				a_error = "no voice in " + a_spec.dir + "\\voices: a Pocket model speaks like the .wav recordings there";
				return false;
			}
			// every clip is read once here: a bad one fails the load instead of a
			// line, and is never skipped, which would renumber the voices after it
			for (const auto& clip : a_out.clips) {
				auto        name = Narrow(clip.stem());
				Audio       audio;
				std::string why;
				if (!ReadClip(clip, CLIP_SECONDS, audio, why)) {
					a_error = "voice clip voices\\" + Narrow(clip.filename()) + ": " + why;
					return false;
				}
				a_out.names.push_back(std::move(name));
			}
			return true;
		}
	}

	std::unique_ptr<Model> Model::Load(const ModelSpec& a_spec, std::string& a_error)
	{
		const auto      dir = U8(a_spec.dir);
		std::error_code ec;
		if (!fs::is_directory(dir, ec)) {
			a_error = "model folder not found: " + a_spec.dir;
			return nullptr;
		}
		const bool  hasVoices = fs::is_regular_file(dir / "voices.bin", ec);
		std::string family = a_spec.family;
		if (family != "kokoro" && family != "vits" && family != "pocket") {
			family = !FindPart(dir, "lm_main").empty() ? "pocket" : hasVoices ? "kokoro" : "vits";
		}

		// what the config points to lives here until the runtime has read it
		PocketFiles pocket;
		std::string model, tokens, voices, lexicon, dataDir;

		SherpaOnnxOfflineTtsConfig config;
		std::memset(&config, 0, sizeof(config));
		if (family == "pocket") {
			if (!FindPocketFiles(a_spec, dir, pocket, a_error)) {
				return nullptr;
			}
			config.model.pocket.lm_flow = pocket.lmFlow.c_str();
			config.model.pocket.lm_main = pocket.lmMain.c_str();
			config.model.pocket.encoder = pocket.encoder.c_str();
			config.model.pocket.decoder = pocket.decoder.c_str();
			config.model.pocket.text_conditioner = pocket.textConditioner.c_str();
			config.model.pocket.vocab_json = pocket.vocab.c_str();
			config.model.pocket.token_scores_json = pocket.scores.c_str();
			// what a clip is turned into, kept per clip; 0 here would keep none, and
			// every line would read its clip afresh
			config.model.pocket.voice_embedding_cache_capacity = std::clamp(static_cast<int>(pocket.clips.size()), 1, 32);
		} else {
			const auto modelName = FindModel(dir);
			if (modelName.empty()) {
				a_error = "no .onnx model in " + a_spec.dir;
				return nullptr;
			}
			if (!fs::is_regular_file(dir / "tokens.txt", ec)) {
				a_error = "tokens.txt missing in " + a_spec.dir;
				return nullptr;
			}
			if (family == "kokoro" && !hasVoices) {
				a_error = "voices.bin missing in " + a_spec.dir + " (Kokoro model)";
				return nullptr;
			}

			std::string missing;
			lexicon = LexiconPaths(a_spec, missing);
			if (!missing.empty()) {
				a_error = "lexicon file not found: " + missing;
				return nullptr;
			}
			dataDir = fs::is_directory(dir / "espeak-ng-data", ec) ? Join(a_spec.dir, "espeak-ng-data") : std::string{};
			if (dataDir.empty() && lexicon.empty()) {
				a_error = "neither espeak-ng-data nor a lexicon in " + a_spec.dir;
				return nullptr;
			}

			model = Join(a_spec.dir, modelName);
			tokens = Join(a_spec.dir, "tokens.txt");
			voices = Join(a_spec.dir, "voices.bin");
			if (family == "kokoro") {
				config.model.kokoro.model = model.c_str();
				config.model.kokoro.voices = voices.c_str();
				config.model.kokoro.tokens = tokens.c_str();
				config.model.kokoro.data_dir = dataDir.c_str();
				config.model.kokoro.lexicon = lexicon.c_str();
				config.model.kokoro.lang = a_spec.lang.c_str();
				config.model.kokoro.length_scale = 1.0f;
			} else {
				config.model.vits.model = model.c_str();
				config.model.vits.tokens = tokens.c_str();
				config.model.vits.data_dir = dataDir.c_str();
				config.model.vits.lexicon = lexicon.c_str();
				config.model.vits.noise_scale = 0.667f;
				config.model.vits.noise_scale_w = 0.8f;
				config.model.vits.length_scale = 1.0f;
			}
		}
		config.model.num_threads = std::clamp(a_spec.threads, 1, 16);
		config.model.provider = "cpu";
		config.max_num_sentences = 1;  // one sentence per step, so a cancel lands between sentences
		config.silence_scale = 0.2f;

		const SherpaOnnxOfflineTts* tts = nullptr;
		try {
			tts = SherpaOnnxCreateOfflineTts(&config);
		} catch (const std::exception& e) {
			a_error = std::string("speech runtime error: ") + e.what();
			return nullptr;
		} catch (...) {
			a_error = "speech runtime error";
			return nullptr;
		}
		if (!tts) {
			a_error = "the speech runtime rejected the model in " + a_spec.dir + " (read as " + family + ")";
			return nullptr;
		}

		std::unique_ptr<Model> loaded(new Model());
		loaded->_tts = tts;
		loaded->_sampleRate = SherpaOnnxOfflineTtsSampleRate(tts);
		loaded->_family = family;
		if (family == "pocket") {
			loaded->_voices = static_cast<int>(pocket.clips.size());
			loaded->_clips = std::move(pocket.clips);
			loaded->_voiceNames = std::move(pocket.names);
		} else {
			loaded->_voices = SherpaOnnxOfflineTtsNumSpeakers(tts);
		}
		return loaded;
	}

	Model::~Model()
	{
		if (_tts) {
			SherpaOnnxDestroyOfflineTts(_tts);
		}
	}

	std::string RuntimeVersion()
	{
		const char* version = SherpaOnnxGetVersionStr();
		return version ? version : "";
	}

	Result Model::Synthesize(const std::string& a_text, int a_voice, float a_speed,
		const std::function<bool()>& a_keepGoing, Audio& a_out, std::string& a_error) const
	{
		a_out = {};

		SherpaOnnxGenerationConfig gen;
		std::memset(&gen, 0, sizeof(gen));
		gen.sid = std::clamp(a_voice, 0, std::max(0, _voices - 1));
		gen.speed = std::clamp(a_speed, 0.5f, 2.0f);
		gen.silence_scale = 0.2f;
		if (!_clips.empty()) {
			// the clip of the voice used last stays read: lines mostly come in runs
			if (_clipVoice != gen.sid) {
				_clipVoice = -1;
				std::string why;
				if (!ReadClip(_clips[static_cast<std::size_t>(gen.sid)], CLIP_SECONDS, _clip, why)) {
					a_error = "voice clip voices\\" + Narrow(_clips[static_cast<std::size_t>(gen.sid)].filename()) + ": " + why;
					return Result::Failed;
				}
				_clipVoice = gen.sid;
			}
			gen.reference_audio = _clip.samples.data();
			gen.reference_audio_len = static_cast<int32_t>(_clip.samples.size());
			gen.reference_sample_rate = _clip.sampleRate;
		}

		const SherpaOnnxGeneratedAudio* audio = nullptr;
		try {
			audio = SherpaOnnxOfflineTtsGenerateWithConfig(_tts, a_text.c_str(), &gen, OnProgress,
				const_cast<void*>(static_cast<const void*>(&a_keepGoing)));
		} catch (const std::exception& e) {
			a_error = std::string("speech runtime error: ") + e.what();
			return Result::Failed;
		} catch (...) {
			a_error = "speech runtime error";
			return Result::Failed;
		}
		if (!audio) {
			a_error = "the speech runtime returned nothing";
			return Result::Failed;
		}
		if (audio->samples && audio->n > 0) {
			a_out.samples.assign(audio->samples, audio->samples + audio->n);
		}
		a_out.sampleRate = audio->sample_rate;
		SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);

		if (a_keepGoing && !a_keepGoing()) {
			return Result::Cancelled;
		}
		if (a_out.samples.empty() || a_out.sampleRate <= 0) {
			a_error = "the speech runtime returned no audio";
			return Result::Failed;
		}
		return Result::Ok;
	}

	void Resample(Audio& a_audio, int a_targetRate)
	{
		if (a_targetRate <= 0 || a_audio.sampleRate <= 0 || a_targetRate == a_audio.sampleRate ||
			a_audio.samples.size() < 4) {
			return;
		}
		const auto&  in = a_audio.samples;
		const auto   last = static_cast<std::ptrdiff_t>(in.size()) - 1;
		const double step = static_cast<double>(a_audio.sampleRate) / static_cast<double>(a_targetRate);
		const auto   count = static_cast<std::size_t>(std::floor(static_cast<double>(last) / step)) + 1;
		const auto   at = [&](std::ptrdiff_t a_i) { return in[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(a_i, 0, last))]; };

		std::vector<float> out(count);
		for (std::size_t k = 0; k < count; ++k) {
			const double pos = static_cast<double>(k) * step;
			const auto   i = static_cast<std::ptrdiff_t>(pos);
			const auto   t = static_cast<float>(pos - static_cast<double>(i));
			const float  p0 = at(i - 1);
			const float  p1 = at(i);
			const float  p2 = at(i + 1);
			const float  p3 = at(i + 2);
			// Catmull-Rom through p1..p2
			out[k] = p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
		}
		a_audio.samples = std::move(out);
		a_audio.sampleRate = a_targetRate;
	}

	std::vector<char> EncodeWav(const Audio& a_audio)
	{
		if (a_audio.samples.empty() || a_audio.sampleRate <= 0) {
			return {};
		}
		const auto dataBytes = static_cast<std::uint32_t>(a_audio.samples.size() * 2);
		const auto rate = static_cast<std::uint32_t>(a_audio.sampleRate);

		std::vector<char> file;
		file.reserve(44 + dataBytes);
		const auto raw = [&](const char* a_bytes, std::size_t a_size) { file.insert(file.end(), a_bytes, a_bytes + a_size); };
		const auto u32 = [&](std::uint32_t a_v) { raw(reinterpret_cast<const char*>(&a_v), 4); };
		const auto u16 = [&](std::uint16_t a_v) { raw(reinterpret_cast<const char*>(&a_v), 2); };
		raw("RIFF", 4);
		u32(36 + dataBytes);
		raw("WAVE", 4);
		raw("fmt ", 4);
		u32(16);
		u16(1);         // PCM
		u16(1);         // mono
		u32(rate);
		u32(rate * 2);  // bytes per second
		u16(2);         // block align
		u16(16);        // bits
		raw("data", 4);
		u32(dataBytes);
		for (const float sample : a_audio.samples) {
			const auto value = static_cast<std::int16_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
			raw(reinterpret_cast<const char*>(&value), 2);
		}
		return file;
	}

	std::string ToSpeakable(std::string_view a_text)
	{
		using namespace std::string_view_literals;
		std::string text(a_text);
		if (!IsUtf8(a_text) && a_text.size() < 0x10000) {
			const auto size = static_cast<int>(a_text.size());
			if (const int wide = ::MultiByteToWideChar(1252, 0, a_text.data(), size, nullptr, 0); wide > 0) {
				std::wstring buffer(static_cast<std::size_t>(wide), L'\0');
				::MultiByteToWideChar(1252, 0, a_text.data(), size, buffer.data(), wide);
				if (const int narrow = ::WideCharToMultiByte(CP_UTF8, 0, buffer.data(), wide, nullptr, 0, nullptr, nullptr);
					narrow > 0) {
					text.assign(static_cast<std::size_t>(narrow), '\0');
					::WideCharToMultiByte(CP_UTF8, 0, buffer.data(), wide, text.data(), narrow, nullptr, nullptr);
				}
			}
		}
		static constexpr std::pair<std::string_view, std::string_view> PLAIN[] = {
			{ "\xE2\x80\x98"sv, "'"sv }, { "\xE2\x80\x99"sv, "'"sv },    // curly single quotes
			{ "\xE2\x80\x9C"sv, "\""sv }, { "\xE2\x80\x9D"sv, "\""sv },  // curly double quotes
			{ "\xE2\x80\xA6"sv, "..."sv },                               // ellipsis
			{ "\xE2\x80\x93"sv, ", "sv }, { "\xE2\x80\x94"sv, ", "sv },  // long dashes: a pause
			{ "\xC2\xA0"sv, " "sv },                                     // no-break space
		};
		for (const auto& [fancy, plain] : PLAIN) {
			for (auto at = text.find(fancy); at != std::string::npos; at = text.find(fancy, at + plain.size())) {
				text.replace(at, fancy.size(), plain);
			}
		}
		return text;
	}

	std::uint64_t Hash64(std::string_view a_text)
	{
		std::uint64_t hash = 0xCBF29CE484222325ull;
		for (const char c : a_text) {
			hash ^= static_cast<std::uint8_t>(c);
			hash *= 0x100000001B3ull;
		}
		return hash;
	}

	bool WriteBytes(const std::filesystem::path& a_path, const void* a_data, std::size_t a_size, std::string& a_error)
	{
		std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
		if (!out) {
			a_error = "cannot open for writing";
			return false;
		}
		out.write(static_cast<const char*>(a_data), static_cast<std::streamsize>(a_size));
		out.close();
		if (!out) {
			a_error = "write failed";
			return false;
		}
		return true;
	}

	bool WriteWav(const std::filesystem::path& a_path, const Audio& a_audio, std::string& a_error)
	{
		const auto file = EncodeWav(a_audio);
		if (file.empty()) {
			a_error = "nothing to write";
			return false;
		}
		return WriteBytes(a_path, file.data(), file.size(), a_error);
	}

	bool ReadWav(const std::filesystem::path& a_path, Audio& a_out)
	{
		a_out = {};
		std::ifstream in(a_path, std::ios::binary);
		if (!in) {
			return false;
		}
		char header[44];
		if (!in.read(header, sizeof(header)) || std::memcmp(header, "RIFF", 4) != 0 ||
			std::memcmp(header + 8, "WAVEfmt ", 8) != 0 || std::memcmp(header + 36, "data", 4) != 0) {
			return false;
		}
		const auto u16 = [&](std::size_t a_off) {
			std::uint16_t v;
			std::memcpy(&v, header + a_off, 2);
			return v;
		};
		const auto u32 = [&](std::size_t a_off) {
			std::uint32_t v;
			std::memcpy(&v, header + a_off, 4);
			return v;
		};
		const auto rate = u32(24);
		const auto dataBytes = u32(40);
		if (u32(16) != 16 || u16(20) != 1 || u16(22) != 1 || u16(34) != 16 || rate < 8000 || rate > 192000 ||
			dataBytes < 2 || dataBytes > 64u * 1024u * 1024u) {
			return false;
		}
		std::vector<std::int16_t> pcm(dataBytes / 2);
		if (!in.read(reinterpret_cast<char*>(pcm.data()), static_cast<std::streamsize>(pcm.size() * 2))) {
			return false;
		}
		a_out.samples.resize(pcm.size());
		for (std::size_t i = 0; i < pcm.size(); ++i) {
			a_out.samples[i] = static_cast<float>(pcm[i]) / 32767.0f;
		}
		a_out.sampleRate = static_cast<int>(rate);
		return true;
	}

	std::uint64_t ModelStamp(const std::string& a_dir)
	{
		const auto      dir = U8(a_dir);
		std::error_code ec;
		if (!fs::is_directory(dir, ec)) {
			return 0;
		}
		const auto modelName = FindModel(dir);
		if (modelName.empty()) {
			return 0;
		}
		std::uint64_t stamp = 0xCBF29CE484222325ull;
		const auto    mix = [&](std::uint64_t a_v) {
			stamp ^= a_v;
			stamp *= 0x100000001B3ull;
		};
		const auto file = [&](const fs::path& a_file) {
			const auto size = fs::file_size(a_file, ec);
			if (ec) {
				ec.clear();
				return false;
			}
			mix(size);
			const auto written = fs::last_write_time(a_file, ec);
			if (!ec) {
				mix(static_cast<std::uint64_t>(written.time_since_epoch().count()));
			}
			ec.clear();
			return true;
		};
		// voices.bin too: a Kokoro voice is replaced there, not in the model file
		for (const auto& name : { modelName, std::string("voices.bin") }) {
			file(dir / U8(name));
		}
		// and a Pocket model's clips, names included: voice numbers follow their order
		for (const auto& clip : ClipsIn(dir)) {
			if (file(clip)) {
				mix(Hash64(Lower(Narrow(clip.filename()))));
			}
		}
		return stamp ? stamp : 1;
	}
}
