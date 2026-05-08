#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace hiroic::wav {

enum class FormatCode : uint16_t {
	Pcm        = 0x0001,
	IeeeFloat  = 0x0003,
	ALaw       = 0x0006,
	MuLaw      = 0x0007,
	Extensible = 0xFFFE,
};

struct Data {
	FormatCode format;
	uint32_t num_channels;
	uint32_t sample_rate;
	uint32_t block_align;
	uint32_t bits_per_sample;
	uint32_t num_samples;
	std::span<const std::byte> samples;
};

std::optional<Data> parse(std::span<const std::byte> buf);

}  // namespace hiroic::wav
