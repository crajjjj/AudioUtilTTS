#include "Lip.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Lip
{
	namespace
	{
		constexpr std::uint32_t CHANNELS = 33;  // 16 mouth shapes + 17 face and head channels
		constexpr std::uint32_t PHONEMES = 16;
		constexpr float         FPS = 30.0f;
		constexpr std::uint32_t LEAD_FRAMES = 1;      // rest frames before the audio starts (the preroll)
		constexpr std::uint32_t MAX_FRAMES = 30 * 120;

		// mouth shape slots, the game's phoneme order
		constexpr std::uint32_t kAah = 0, kBigAah = 1, kBMP = 2, kChJSh = 3, kDST = 4, kEee = 5, kEh = 6, kFV = 7,
								kI = 8, kK = 9, kN = 10, kOh = 11, kOohQ = 12, kR = 13, kTh = 14, kW = 15;

		// syllable cutting and shaping, the values AudioUtil's own loudness lipsync settled on
		constexpr float         VOICED_FLOOR = 0.05f;
		constexpr float         VALLEY_RATIO = 0.6f;
		constexpr std::uint32_t MIN_SYLLABLE_FRAMES = 3;
		constexpr std::uint32_t GAP_FRAMES = 5;  // a pause this long closes the lips before the next syllable
		constexpr float         CLOSURE = 0.85f;
		constexpr float         ATTACK_TAU = 0.03f;
		constexpr float         RELEASE_TAU = 0.09f;

		// splitmix64 finalizer: a stateless pick per syllable
		std::uint64_t Mix64(std::uint64_t a_x)
		{
			a_x += 0x9E3779B97F4A7C15ull;
			a_x = (a_x ^ (a_x >> 30)) * 0xBF58476D1CE4E5B9ull;
			a_x = (a_x ^ (a_x >> 27)) * 0x94D049BB133111EBull;
			return a_x ^ (a_x >> 31);
		}

		// loudness 100 times a second, peak-normalized and lifted so mid-loud
		// speech still opens the mouth; empty for silence
		std::vector<float> Envelope(const TtsEngine::Audio& a_audio)
		{
			std::vector<float> levels;
			if (a_audio.sampleRate <= 0 || a_audio.samples.empty()) {
				return levels;
			}
			const auto window = static_cast<std::size_t>(std::max(1, a_audio.sampleRate / 100));
			levels.reserve(a_audio.samples.size() / window + 1);
			float peak = 0.0f;
			for (std::size_t start = 0; start < a_audio.samples.size(); start += window) {
				const auto end = std::min(start + window, a_audio.samples.size());
				double     sum = 0.0;
				for (std::size_t i = start; i < end; ++i) {
					sum += static_cast<double>(a_audio.samples[i]) * a_audio.samples[i];
				}
				const auto level = static_cast<float>(std::sqrt(sum / static_cast<double>(end - start)));
				levels.push_back(level);
				peak = std::max(peak, level);
			}
			if (peak < 1.0e-4f) {
				return {};
			}
			for (float& level : levels) {
				level = std::pow(std::clamp(level / peak, 0.0f, 1.0f), 0.6f);
			}
			return levels;
		}

		float Sample(const std::vector<float>& a_levels, float a_seconds)
		{
			const float pos = a_seconds * 100.0f;
			const auto  index = static_cast<std::size_t>(pos);
			if (index + 1 >= a_levels.size()) {
				return index < a_levels.size() ? a_levels[index] : 0.0f;
			}
			const float frac = pos - static_cast<float>(index);
			return a_levels[index] + (a_levels[index + 1] - a_levels[index]) * frac;
		}

		// a_grid is frame-major, a_frames x CHANNELS
		std::vector<std::uint8_t> Encode(const std::vector<float>& a_grid, std::uint32_t a_frames, std::int32_t a_preroll)
		{
			std::vector<std::uint8_t> plain(12 + a_grid.size() * sizeof(float));
			const std::uint32_t       tracks = PHONEMES;
			std::memcpy(plain.data(), &a_frames, 4);
			std::memcpy(plain.data() + 4, &a_preroll, 4);
			std::memcpy(plain.data() + 8, &tracks, 4);
			if (!a_grid.empty()) {
				std::memcpy(plain.data() + 12, a_grid.data(), a_grid.size() * sizeof(float));
			}

			std::vector<std::uint8_t> out;
			out.reserve(plain.size() / 3 + 16);
			const auto u32 = [&](std::uint32_t a_v) {
				const auto* bytes = reinterpret_cast<const std::uint8_t*>(&a_v);
				out.insert(out.end(), bytes, bytes + 4);
			};
			u32(1);
			u32(132 * a_frames + 28);
			u32(13);
			for (std::size_t i = 0; i < plain.size();) {
				if (plain[i] != 0) {
					out.push_back(plain[i++]);
					continue;
				}
				std::size_t run = 0;
				while (i + run < plain.size() && plain[i + run] == 0 && run < 0xFFFF) {
					++run;
				}
				out.push_back(0);
				out.push_back(static_cast<std::uint8_t>(run & 0xFF));
				out.push_back(static_cast<std::uint8_t>(run >> 8));
				i += run;
			}
			return out;
		}

		// A frame count whose low byte is zero would start the stream with a zero
		// run and shift every later header field; one more rest frame avoids the
		// layout that only some readers of this format understand.
		std::uint32_t PadFrames(std::uint32_t a_frames)
		{
			return (a_frames & 0xFF) == 0 ? a_frames + 1 : a_frames;
		}
	}

	std::vector<std::uint8_t> Rest()
	{
		const std::uint32_t frames = 2;
		return Encode(std::vector<float>(static_cast<std::size_t>(frames) * CHANNELS, 0.0f), frames,
			-static_cast<std::int32_t>(LEAD_FRAMES));
	}

	std::vector<std::uint8_t> FromAudio(const TtsEngine::Audio& a_audio, std::string_view a_seed, float a_strength)
	{
		const auto envelope = Envelope(a_audio);
		const auto voicedFrames = std::min(static_cast<std::uint32_t>(a_audio.Seconds() * FPS) + 1, MAX_FRAMES);
		if (envelope.empty() || voicedFrames < 3) {
			return Rest();
		}

		std::vector<float> level(voicedFrames);
		for (std::uint32_t f = 0; f < voicedFrames; ++f) {
			const float sample = Sample(envelope, static_cast<float>(f) / FPS);
			level[f] = sample >= VOICED_FLOOR ? sample : 0.0f;
		}

		// voiced runs, split at deep valleys (a local low under 60% of the loudest
		// frame since the last split): each piece is a syllable
		struct Syllable
		{
			std::uint32_t start;
			std::uint32_t end;  // exclusive
		};
		std::vector<Syllable> syllables;
		std::uint32_t         runStart = UINT32_MAX;
		for (std::uint32_t f = 0; f <= voicedFrames; ++f) {
			const bool on = f < voicedFrames && level[f] > 0.0f;
			if (on && runStart == UINT32_MAX) {
				runStart = f;
			} else if (!on && runStart != UINT32_MAX) {
				std::uint32_t segStart = runStart;
				float         peak = level[runStart];
				for (std::uint32_t i = runStart + 1; i + 1 < f; ++i) {
					peak = std::max(peak, level[i]);
					if (level[i] <= level[i - 1] && level[i] <= level[i + 1] && level[i] < VALLEY_RATIO * peak &&
						i - segStart >= MIN_SYLLABLE_FRAMES) {
						syllables.push_back({ segStart, i });
						segStart = i;
						peak = level[i];
					}
				}
				syllables.push_back({ segStart, f });
				runStart = UINT32_MAX;
			}
		}
		if (syllables.empty()) {
			return Rest();
		}

		// How often each shape opens a syllable in recorded game speech (AudioUtil
		// measured it over 638 vanilla lip files). The lip press is not in the
		// palette: closures own it, below.
		struct Weight
		{
			std::uint32_t ch;
			float         weight;
			bool          wideOpen;
		};
		static constexpr Weight PALETTE[] = {
			{ kAah, 13.0f, true }, { kBigAah, 4.0f, true }, { kOh, 6.0f, true }, { kOohQ, 6.0f, false },
			{ kEh, 12.0f, false }, { kEee, 5.0f, false }, { kI, 6.0f, false }, { kN, 10.0f, false },
			{ kDST, 7.0f, false }, { kR, 6.0f, false }, { kFV, 4.0f, false }, { kTh, 4.0f, false },
			{ kK, 3.0f, false }, { kW, 3.0f, false }, { kChJSh, 3.0f, false },
		};
		constexpr std::size_t N = std::size(PALETTE);
		float                 total = 0.0f;
		for (const auto& entry : PALETTE) {
			total += entry.weight;
		}

		std::vector<std::vector<float>> curve(PHONEMES, std::vector<float>(voicedFrames, 0.0f));
		const std::uint64_t             seed = TtsEngine::Hash64(a_seed);
		std::size_t                     prevPick = N;
		std::uint32_t                   prevEnd = 0;
		for (std::size_t s = 0; s < syllables.size(); ++s) {
			const auto& syl = syllables[s];
			// never the shape the syllable before it had
			const float excluded = prevPick < N ? PALETTE[prevPick].weight : 0.0f;
			float       r = static_cast<float>(Mix64(seed + s) % 100000) / 100000.0f * (total - excluded);
			std::size_t pick = prevPick == 0 ? 1 : 0;
			for (std::size_t i = 0; i < N; ++i) {
				if (i == prevPick) {
					continue;
				}
				if (r < PALETTE[i].weight) {
					pick = i;
					break;
				}
				r -= PALETTE[i].weight;
			}
			prevPick = pick;

			const std::uint32_t ch = PALETTE[pick].ch;
			for (std::uint32_t f = syl.start; f < syl.end; ++f) {
				curve[ch][f] = level[f];
				if (PALETTE[pick].wideOpen && ch != kBigAah) {
					// a loud open vowel spills into the wide shape
					curve[kBigAah][f] = std::max(curve[kBigAah][f], std::max(0.0f, level[f] - 0.55f) / 0.45f * 0.35f);
				}
			}
			// the lips press shut just before a syllable that follows a real pause
			if (syl.start >= 2 && syl.start - prevEnd >= GAP_FRAMES) {
				curve[kBMP][syl.start - 2] = CLOSURE * 0.65f;
				curve[kBMP][syl.start - 1] = CLOSURE;
			}
			prevEnd = syl.end;
		}

		// the jaw follows the loudness under whatever shape was picked, or narrow
		// shapes would leave the mouth barely moving
		for (std::uint32_t f = 0; f < voicedFrames; ++f) {
			curve[kAah][f] = std::max(curve[kAah][f], level[f]);
		}

		// quick to open, slower to close; the closures stay crisp
		const float dt = 1.0f / FPS;
		const float attack = 1.0f - std::exp(-dt / ATTACK_TAU);
		const float release = 1.0f - std::exp(-dt / RELEASE_TAU);
		for (std::uint32_t ch = 0; ch < PHONEMES; ++ch) {
			if (ch == kBMP) {
				continue;
			}
			float current = 0.0f;
			for (float& value : curve[ch]) {
				current += (value - current) * (value > current ? attack : release);
				value = current;
			}
		}

		// one rest frame leads in (the preroll) and one closes the line
		const std::uint32_t frames = PadFrames(LEAD_FRAMES + voicedFrames + 1);
		const float         strength = std::clamp(a_strength, 0.0f, 1.5f);
		std::vector<float>  grid(static_cast<std::size_t>(frames) * CHANNELS, 0.0f);
		for (std::uint32_t ch = 0; ch < PHONEMES; ++ch) {
			for (std::uint32_t f = 0; f < voicedFrames; ++f) {
				float value = std::clamp(curve[ch][f] * strength, 0.0f, 1.0f);
				if (value < 0.004f) {
					continue;  // rest stays exactly zero
				}
				// keep the low byte nonzero, so a weight never begins with zero
				// bytes: readers that take the stream's first zero run as a count
				// of whole rest cells stay aligned
				std::uint32_t bits;
				std::memcpy(&bits, &value, 4);
				bits |= 1;
				std::memcpy(&value, &bits, 4);
				grid[static_cast<std::size_t>(LEAD_FRAMES + f) * CHANNELS + ch] = value;
			}
		}
		return Encode(grid, frames, -static_cast<std::int32_t>(LEAD_FRAMES));
	}

	bool Write(const std::filesystem::path& a_path, const std::vector<std::uint8_t>& a_bytes, std::string& a_error)
	{
		return TtsEngine::WriteBytes(a_path, a_bytes.data(), a_bytes.size(), a_error);
	}
}
