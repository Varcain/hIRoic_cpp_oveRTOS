#include "dsp.hpp"
#include <ove/ove.hpp>
#include <algorithm>

/* POSIX / non-ARM passthrough — matches C reference's dsp_stub.c.
 * The full CMSIS-DSP overlap-add FFT convolution lives in dsp.cpp
 * and is only compiled in for STM32F746 builds (see app.yaml
 * board_sources.stm32f746g-discovery). */

namespace hiroic::dsp {

void init()
{
	OVE_LOG_INF("DSP stub: passthrough mode");
}

void process(std::span<int16_t> out, std::span<const int16_t> in)
{
	const size_t n = std::min(out.size(), in.size());
	std::copy_n(in.begin(), n, out.begin());
}

void load_ir(std::span<const int32_t> ir, uint32_t sample_rate)
{
	OVE_LOG_INF("DSP stub: IR load ignored (%zu samples @ %u Hz)",
		    ir.size(), sample_rate);
}

}  // namespace hiroic::dsp
