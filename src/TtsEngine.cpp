#include "TtsEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <system_error>

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

		int32_t OnProgress(const float*, int32_t, float, void* a_arg)
		{
			const auto* keepGoing = static_cast<const std::function<bool()>*>(a_arg);
			return (!*keepGoing || (*keepGoing)()) ? 1 : 0;
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
		const auto modelName = FindModel(dir);
		if (modelName.empty()) {
			a_error = "no .onnx model in " + a_spec.dir;
			return nullptr;
		}
		if (!fs::is_regular_file(dir / "tokens.txt", ec)) {
			a_error = "tokens.txt missing in " + a_spec.dir;
			return nullptr;
		}

		const bool  hasVoices = fs::is_regular_file(dir / "voices.bin", ec);
		std::string family = a_spec.family;
		if (family != "kokoro" && family != "vits") {
			family = hasVoices ? "kokoro" : "vits";
		}
		if (family == "kokoro" && !hasVoices) {
			a_error = "voices.bin missing in " + a_spec.dir + " (Kokoro model)";
			return nullptr;
		}

		std::string       missing;
		const std::string lexicon = LexiconPaths(a_spec, missing);
		if (!missing.empty()) {
			a_error = "lexicon file not found: " + missing;
			return nullptr;
		}
		const std::string dataDir =
			fs::is_directory(dir / "espeak-ng-data", ec) ? Join(a_spec.dir, "espeak-ng-data") : std::string{};
		if (dataDir.empty() && lexicon.empty()) {
			a_error = "neither espeak-ng-data nor a lexicon in " + a_spec.dir;
			return nullptr;
		}

		const std::string model = Join(a_spec.dir, modelName);
		const std::string tokens = Join(a_spec.dir, "tokens.txt");
		const std::string voices = Join(a_spec.dir, "voices.bin");

		SherpaOnnxOfflineTtsConfig config;
		std::memset(&config, 0, sizeof(config));
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
		loaded->_voices = SherpaOnnxOfflineTtsNumSpeakers(tts);
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

	bool WriteWav(const std::filesystem::path& a_path, const Audio& a_audio, std::string& a_error)
	{
		if (a_audio.samples.empty() || a_audio.sampleRate <= 0) {
			a_error = "nothing to write";
			return false;
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

		std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
		if (!out) {
			a_error = "cannot open for writing";
			return false;
		}
		out.write(file.data(), static_cast<std::streamsize>(file.size()));
		out.close();
		if (!out) {
			a_error = "write failed";
			return false;
		}
		return true;
	}
}
