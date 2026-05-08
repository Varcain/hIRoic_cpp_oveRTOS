#include "ir_convert.hpp"
#include "app_conf.hpp"
#include <ove/ove.hpp>

namespace hiroic::ir {

std::optional<Converted> convert_samples(const wav::Data &wav,
					 std::span<int32_t> ir_buf)
{
	if (wav.num_channels > 1) {
		OVE_LOG_ERR("ir: unsupported channels %u", wav.num_channels);
		return std::nullopt;
	}
	if (wav.num_samples > kIrMaxLen || wav.num_samples > ir_buf.size()) {
		OVE_LOG_ERR("ir: sample count %u > capacity", wav.num_samples);
		return std::nullopt;
	}
	if (wav.block_align == 0 || wav.block_align > (kDspBitSize / 8)) {
		OVE_LOG_ERR("ir: invalid block align %u", wav.block_align);
		return std::nullopt;
	}

	const std::byte *src = wav.samples.data();
	const uint32_t ba = wav.block_align;

	for (uint32_t i = 0; i < wav.num_samples; ++i) {
		int32_t acc = 0;
		for (uint32_t j = 0; j < ba; ++j) {
			acc |= static_cast<int32_t>(
				std::to_integer<uint8_t>(src[i * ba + j]))
				<< (j * 8);
		}
		ir_buf[i] = acc;
	}

	if (ba < 4) {
		int32_t sign_bit = 1 << (ba * 8 - 1);
		for (uint32_t i = 0; i < wav.num_samples; ++i) {
			ir_buf[i] = (ir_buf[i] ^ sign_bit) - sign_bit;
		}
	}

	if (wav.bits_per_sample != kDspBitSize) {
		if (kDspBitSize > wav.bits_per_sample) {
			uint32_t shift = kDspBitSize - wav.bits_per_sample;
			for (uint32_t i = 0; i < wav.num_samples; ++i)
				ir_buf[i] <<= shift;
		} else {
			uint32_t shift = wav.bits_per_sample - kDspBitSize;
			for (uint32_t i = 0; i < wav.num_samples; ++i)
				ir_buf[i] >>= shift;
		}
	}

	OVE_LOG_INF("ir: converted %u samples @ %u Hz", wav.num_samples,
		    wav.sample_rate);
	return Converted{wav.num_samples, wav.sample_rate};
}

}  // namespace hiroic::ir
