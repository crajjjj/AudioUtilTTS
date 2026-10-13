#include "TtsEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
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

	std::string CleanLine(std::string_view a_text, int a_maxChars)
	{
		const auto  text = ToSpeakable(a_text);
		std::string out;
		out.reserve(text.size());
		bool space = false;
		for (const char c : text) {
			const auto u = static_cast<unsigned char>(c);
			if (u <= 0x20 || u == 0x7F) {
				space = !out.empty();
				continue;
			}
			if (space) {
				out += ' ';
				space = false;
			}
			out += c;
		}

		const auto cap = static_cast<std::size_t>(a_maxChars);
		if (out.size() <= cap) {
			return out;
		}
		auto cut = out.find_last_of(".!?", cap - 1);
		if (cut != std::string::npos && cut >= cap / 2) {
			return out.substr(0, cut + 1);
		}
		cut = out.rfind(' ', cap - 1);
		if (cut != std::string::npos && cut >= cap / 2) {
			return out.substr(0, cut);
		}
		cut = cap;
		while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
			--cut;  // never split a UTF-8 sequence
		}
		return out.substr(0, cut);
	}

	namespace
	{
		// A line with nothing in it to say. Mods write lines that are not speech:
		// "..." for a silent beat, "(...)" or "[...]" as a stand-in, a lone dash, a
		// row of asterisks. A voice model handed one of those makes a noise where
		// the game means silence, so they are told apart here: true when the text
		// holds no letter and no digit, only punctuation, brackets, symbols and
		// spaces, plain or not. a_text is UTF-8.
		bool IsPlaceholder(std::string_view a_text)
		{
			for (std::size_t i = 0; i < a_text.size();) {
				const auto lead = static_cast<unsigned char>(a_text[i]);
				if (lead < 0x80) {
					if (std::isalnum(lead) != 0) {
						return false;
					}
					++i;
					continue;
				}
				// one UTF-8 sequence; a broken one is not a letter
				const std::size_t extra = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
				char32_t          point = extra == 3 ? lead & 0x07 : extra == 2 ? lead & 0x0F : extra == 1 ? lead & 0x1F : 0xFFFD;
				std::size_t       k = 1;
				for (; k <= extra && i + k < a_text.size() && (static_cast<unsigned char>(a_text[i + k]) & 0xC0) == 0x80;
					 ++k) {
					point = (point << 6) | (static_cast<unsigned char>(a_text[i + k]) & 0x3F);
				}
				if (k <= extra) {
					point = 0xFFFD;
				}
				i += k;
				// the blocks that hold marks, not letters: Latin-1 signs, general
				// punctuation, arrows / maths / boxes / dingbats, CJK punctuation,
				// small and full-width forms of ASCII punctuation
				const bool mark = (point >= 0x00A0 && point <= 0x00BF) || point == 0x00D7 || point == 0x00F7 ||
								  (point >= 0x2000 && point <= 0x2BFF) || (point >= 0x3000 && point <= 0x303F) ||
								  (point >= 0xFE30 && point <= 0xFE6F) || (point >= 0xFF00 && point <= 0xFF0F) ||
								  point == 0xFFFD;
				if (!mark) {
					return false;
				}
			}
			return true;
		}
	}

	std::string CleanDialogue(std::string_view a_text, bool a_skipDirections, int a_maxChars)
	{
		std::string spoken;
		spoken.reserve(a_text.size());
		for (std::size_t i = 0; i < a_text.size(); ++i) {
			const char c = a_text[i];
			char       closer = 0;
			if (c == '<') {
				closer = '>';
			} else if (a_skipDirections) {
				closer = c == '(' ? ')' : c == '[' ? ']' : c == '*' ? '*' : 0;
			}
			// an opener with nothing closing it is ordinary text
			if (const auto end = closer ? a_text.find(closer, i + 1) : std::string_view::npos;
				end != std::string_view::npos) {
				spoken += ' ';
				i = end;
				continue;
			}
			spoken += c;
		}
		auto out = CleanLine(spoken, a_maxChars);
		return IsPlaceholder(out) ? std::string{} : out;
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

	std::string LineFileName(std::string_view a_cleanText, int a_voice, float a_speed)
	{
		// The speed in thousandths, as a whole number: printing a fraction would
		// follow the process locale, and the name has to be the same everywhere.
		char head[48];
		std::snprintf(head, sizeof(head), "%d|%ld|", a_voice, std::lround(static_cast<double>(a_speed) * 1000.0));
		std::string key(head);
		key += a_cleanText;
		char name[32];
		std::snprintf(name, sizeof(name), "%016llx.wav", static_cast<unsigned long long>(Hash64(key)));
		return name;
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
		// voices.bin too: a Kokoro voice is replaced there, not in the model file
		for (const auto& name : { modelName, std::string("voices.bin") }) {
			const auto file = dir / U8(name);
			const auto size = fs::file_size(file, ec);
			if (ec) {
				ec.clear();
				continue;
			}
			mix(size);
			const auto written = fs::last_write_time(file, ec);
			if (!ec) {
				mix(static_cast<std::uint64_t>(written.time_since_epoch().count()));
			}
			ec.clear();
		}
		return stamp ? stamp : 1;
	}
}
