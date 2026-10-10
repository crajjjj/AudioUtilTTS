#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "TtsEngine.h"

// Mouth animation for a generated line, written in the game's own .lip format,
// so the engine lipsyncs a generated dialogue line the way it does a recorded
// one. No game dependencies: tools\probe writes the same file outside Skyrim.
//
// The speech runtime returns audio only, no phoneme timing, so the shapes are
// guessed from loudness: the line is cut into syllables, each syllable opens
// one mouth shape picked from a speech-weighted palette, the jaw follows the
// loudness underneath, and the lips press shut after a real pause. The pick is
// seeded by the text, so a line always mouths the same way. It reads as
// talking; it does not form the actual sounds.
//
// File layout (see AudioUtil, tools\lipresearch): u32 version = 1, u32 size =
// 132 * frames + 28, u32 13, then one zero-run-length stream (a nonzero byte is
// a literal, 00 + u16 count is that many zero bytes) holding u32 frames, i32
// preroll, u32 16 and a dense float32 grid of frames x 33 weights at 30 frames
// per second: 16 mouth shapes, then 17 face and head channels left at rest.
namespace Lip
{
	// a_strength scales every mouth shape (1.0 = as synthesized). Returns Rest()
	// for audio too short or too quiet to animate.
	std::vector<std::uint8_t> FromAudio(const TtsEngine::Audio& a_audio, std::string_view a_seed, float a_strength);

	// A mouth that stays shut. What a placeholder holds while no line is in it.
	std::vector<std::uint8_t> Rest();

	bool Write(const std::filesystem::path& a_path, const std::vector<std::uint8_t>& a_bytes, std::string& a_error);
}
