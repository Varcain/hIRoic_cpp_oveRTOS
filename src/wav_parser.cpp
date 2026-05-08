#include "wav_parser.hpp"
#include "app_conf.hpp"
#include <ove/ove.hpp>
#include <algorithm>
#include <cstring>

namespace hiroic::wav {

namespace {

constexpr uint32_t kChunkRiff = 0x46464952u;  // "RIFF"
constexpr uint32_t kChunkWave = 0x45564157u;  // "WAVE"
constexpr uint32_t kChunkFmt  = 0x20746D66u;  // "fmt "
constexpr uint32_t kChunkData = 0x61746164u;  // "data"

constexpr size_t kFmtChunkMin = 16;
constexpr size_t kChunkHeader = 8;

uint32_t read_u32(const std::byte *p)
{
	uint32_t v;
	std::memcpy(&v, p, 4);
	return v;
}

uint16_t read_u16(const std::byte *p)
{
	uint16_t v;
	std::memcpy(&v, p, 2);
	return v;
}

}  // namespace

std::optional<Data> parse(std::span<const std::byte> buf)
{
	if (buf.size() < 12) {
		OVE_LOG_ERR("wav: buffer too small");
		return std::nullopt;
	}

	const std::byte *p = buf.data();
	if (read_u32(p) != kChunkRiff) {
		OVE_LOG_ERR("wav: bad RIFF id");
		return std::nullopt;
	}
	uint32_t riff_size = read_u32(p + 4);
	if (read_u32(p + 8) != kChunkWave) {
		OVE_LOG_ERR("wav: bad WAVE id");
		return std::nullopt;
	}
	if (riff_size < 4 || riff_size > UINT32_MAX - 8) {
		OVE_LOG_ERR("wav: invalid RIFF size");
		return std::nullopt;
	}

	size_t end = std::min<size_t>(buf.size(), 8 + riff_size);
	size_t offset = 12;

	const std::byte *fmt = nullptr;
	const std::byte *data = nullptr;
	uint32_t data_size = 0;

	while (offset + kChunkHeader <= end) {
		uint32_t id = read_u32(p + offset);
		uint32_t size = read_u32(p + offset + 4);

		if (id == kChunkFmt) {
			if (size < kFmtChunkMin) {
				OVE_LOG_ERR("wav: fmt chunk too small");
				return std::nullopt;
			}
			fmt = p + offset + kChunkHeader;
		} else if (id == kChunkData) {
			data = p + offset + kChunkHeader;
			data_size = size;
			break;
		}
		offset += kChunkHeader + size;
	}

	if (!fmt) {
		OVE_LOG_ERR("wav: missing fmt");
		return std::nullopt;
	}
	if (!data) {
		OVE_LOG_ERR("wav: missing data");
		return std::nullopt;
	}

	Data d{};
	d.format          = static_cast<FormatCode>(read_u16(fmt));
	d.num_channels    = read_u16(fmt + 2);
	d.sample_rate     = read_u32(fmt + 4);
	d.block_align     = read_u16(fmt + 12);
	d.bits_per_sample = read_u16(fmt + 14);

	if (d.format != FormatCode::Pcm) {
		OVE_LOG_ERR("wav: unsupported format 0x%x",
			    static_cast<unsigned>(d.format));
		return std::nullopt;
	}
	if (d.bits_per_sample != 16 && d.bits_per_sample != 24
	    && d.bits_per_sample != 32) {
		OVE_LOG_ERR("wav: unsupported bps %u", d.bits_per_sample);
		return std::nullopt;
	}
	if (d.num_channels < 1) {
		OVE_LOG_ERR("wav: invalid channels %u", d.num_channels);
		return std::nullopt;
	}
	if (d.sample_rate == 0) {
		OVE_LOG_ERR("wav: invalid sample rate");
		return std::nullopt;
	}
	if (d.block_align != d.num_channels * (d.bits_per_sample / 8)) {
		OVE_LOG_ERR("wav: block_align mismatch");
		return std::nullopt;
	}
	uint32_t byte_rate = read_u32(fmt + 8);
	if (byte_rate != d.sample_rate * d.block_align) {
		OVE_LOG_ERR("wav: byte_rate mismatch");
		return std::nullopt;
	}
	if (data_size == 0) {
		OVE_LOG_ERR("wav: empty data");
		return std::nullopt;
	}

	d.num_samples = data_size / d.block_align;
	if (d.num_samples > kIrMaxLen) {
		OVE_LOG_WRN("wav: truncated %u to %u samples", d.num_samples,
			    kIrMaxLen);
		d.num_samples = kIrMaxLen;
	}
	d.samples = std::span{data,
			      static_cast<size_t>(d.num_samples) * d.block_align};
	return d;
}

}  // namespace hiroic::wav
