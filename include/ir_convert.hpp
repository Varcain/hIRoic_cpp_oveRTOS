#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include "wav_parser.hpp"

namespace hiroic::ir {

struct Converted {
	uint32_t length;
	uint32_t sample_rate;
};

constexpr bool is_wav(std::string_view filename)
{
	if (filename.size() < 4)
		return false;
	auto ext = filename.substr(filename.size() - 4);
	auto lo = [](char c) {
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c | 0x20) : c;
	};
	return ext[0] == '.' && lo(ext[1]) == 'w' && lo(ext[2]) == 'a'
	       && lo(ext[3]) == 'v';
}

std::optional<Converted> convert_samples(const wav::Data &wav,
					 std::span<int32_t> ir_buf);

}  // namespace hiroic::ir
