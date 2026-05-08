/*
 * hIRoic DSP — FFT-based overlap-add convolution via CMSIS-DSP (ARM only).
 * Only compiled for board stm32f746g-discovery (see app.yaml board_sources);
 * POSIX and other platforms build dsp_stub.cpp.
 */

#include "dsp.hpp"
#include "app_conf.hpp"
#include <ove/ove.hpp>

#include "arm_math.h"
#include "arm_const_structs.h"

#include <atomic>
#include <algorithm>
#include <cstring>

namespace hiroic::dsp {

namespace {

/* -------------------------------------------------------------------------- */
/*  Memory placement                                                           */
/*                                                                             */
/*  DSP_DTCM  — 64-bit tightly-coupled RAM for butterfly-heavy working buffers. */
/*  DSP_SDRAM — external SDRAM for IR frequency-response tables.               */
/* -------------------------------------------------------------------------- */

#if defined(__ZEPHYR__)
#include <zephyr/devicetree.h>
#include <zephyr/linker/sections.h>
#include <zephyr/linker/devicetree_regions.h>
#define DSP_DTCM  __dtcm_bss_section
#define DSP_SDRAM Z_GENERIC_SECTION(LINKER_DT_NODE_REGION_NAME(DT_NODELABEL(sdram1)))
#else
#define DSP_DTCM  __attribute__((section(".dsp_bss")))
#define DSP_SDRAM __attribute__((section(".sdram_bss")))
#endif

/* -------------------------------------------------------------------------- */
/*  Defaults: FFT sizes derived from buffer + IR length                        */
/* -------------------------------------------------------------------------- */

constexpr unsigned kFftSizeMax = []() consteval {
	unsigned n = kDspBufferSize + kIrMaxLen - 1;
	unsigned p = 16;
	while (p < n && p < 4096)
		p <<= 1;
	return std::min(p, 4096u);
}();

/* -------------------------------------------------------------------------- */
/*  State                                                                      */
/* -------------------------------------------------------------------------- */

struct IrState {
	q31_t ir_fft[kFftSizeMax * 2];
	const arm_cfft_instance_q31 *fft_instance;
	unsigned fft_size;
	unsigned fft_log2n;
	unsigned overlap_size;
	unsigned ir_boost_bits;
};

/* IR FFT tables — large, sequential access, park in SDRAM. */
DSP_SDRAM IrState g_state_a;
DSP_SDRAM IrState g_state_b;

/* Audio-thread owned working buffers — butterfly-heavy, pin to DTCM. */
DSP_DTCM q31_t g_input_fft[kFftSizeMax * 2];
DSP_DTCM q31_t g_output_fft[kFftSizeMax * 2];
DSP_DTCM q31_t g_overlap_buffer[kFftSizeMax];
DSP_DTCM q31_t g_work_buffer[kFftSizeMax];

std::atomic<IrState *> g_active_ir{&g_state_a};

/* -------------------------------------------------------------------------- */
/*  FFT instance table dispatch                                                */
/* -------------------------------------------------------------------------- */

const arm_cfft_instance_q31 *fft_instance_for(unsigned size)
{
	switch (size) {
	case   16: return &arm_cfft_sR_q31_len16;
	case   32: return &arm_cfft_sR_q31_len32;
	case   64: return &arm_cfft_sR_q31_len64;
	case  128: return &arm_cfft_sR_q31_len128;
	case  256: return &arm_cfft_sR_q31_len256;
	case  512: return &arm_cfft_sR_q31_len512;
	case 1024: return &arm_cfft_sR_q31_len1024;
	case 2048: return &arm_cfft_sR_q31_len2048;
	case 4096: return &arm_cfft_sR_q31_len4096;
	default:
		OVE_LOG_ERR("dsp: unsupported FFT size %u", size);
		return nullptr;
	}
}

unsigned next_power_of_2(unsigned n, unsigned cap = 4096)
{
	unsigned p = 1;
	while (p < n && p < cap)
		p <<= 1;
	return std::min(p, cap);
}

void set_fft_size(IrState &s, unsigned ir_len)
{
	unsigned min_fft = kDspBufferSize + ir_len - 1;
	if (min_fft > 4096) {
		OVE_LOG_ERR("dsp: required FFT size %u > 4096", min_fft);
		s.fft_size = 0;
		s.overlap_size = 0;
		s.fft_log2n = 0;
		s.fft_instance = nullptr;
		return;
	}
	s.fft_size = next_power_of_2(min_fft);
	s.overlap_size = s.fft_size - kDspBufferSize;
	s.fft_instance = fft_instance_for(s.fft_size);

	unsigned tmp = s.fft_size;
	s.fft_log2n = 0;
	while (tmp > 1) {
		tmp >>= 1;
		s.fft_log2n++;
	}

	if (!s.fft_instance) {
		s.fft_size = 0;
		s.fft_log2n = 0;
		s.overlap_size = 0;
	}
}

}  // namespace

/* -------------------------------------------------------------------------- */
/*  Public interface                                                           */
/* -------------------------------------------------------------------------- */

void init()
{
	std::memset(&g_state_a, 0, sizeof(g_state_a));
	std::memset(&g_state_b, 0, sizeof(g_state_b));
	std::memset(g_overlap_buffer, 0, sizeof(g_overlap_buffer));
	g_active_ir.store(&g_state_a, std::memory_order_release);

	/* No IR loaded — process() falls through to passthrough until
	 * load_ir() is called with a real IR. */
}

void process(std::span<int16_t> out, std::span<const int16_t> in)
{
	IrState *ir = g_active_ir.load(std::memory_order_acquire);
	unsigned fs = ir->fft_size;
	unsigned log2n = ir->fft_log2n;
	unsigned ovl = ir->overlap_size;

	if (!ir->fft_instance || fs == 0) {
		const size_t n = std::min<size_t>({out.size(), in.size(),
						   kDspBufferSize});
		std::copy_n(in.begin(), n, out.begin());
		return;
	}

	/* int16 → Q31 complex (real in high half, imag = 0) */
	for (unsigned i = 0; i < kDspBufferSize; ++i) {
		g_input_fft[i * 2]     = static_cast<q31_t>(in[i]) << 16;
		g_input_fft[i * 2 + 1] = 0;
	}
	std::memset(&g_input_fft[kDspBufferSize * 2], 0,
		    (fs - kDspBufferSize) * 2 * sizeof(q31_t));

	arm_cfft_q31(ir->fft_instance, g_input_fft, 0, 1);
	arm_cmplx_mult_cmplx_q31(g_input_fft, ir->ir_fft, g_output_fft, fs);
	arm_cfft_q31(ir->fft_instance, g_output_fft, 1, 1);

	for (unsigned i = 0; i < fs; ++i)
		g_work_buffer[i] = g_output_fft[i * 2];

	arm_add_q31(g_work_buffer, g_overlap_buffer, g_work_buffer, ovl);
	for (unsigned i = 0; i < ovl; ++i)
		g_overlap_buffer[i] = g_work_buffer[kDspBufferSize + i];

	/* Q31 → int16 with the FFT/mult gain recovery applied at output:
	 *   gain_bits = 2*log2(N) + 2 − ir_boost_bits. */
	unsigned gain_bits = 2 * log2n + 2 - ir->ir_boost_bits;
	for (unsigned i = 0; i < kDspBufferSize; ++i) {
		int64_t v;
		if (gain_bits <= 16)
			v = static_cast<int64_t>(g_work_buffer[i])
				>> (16 - gain_bits);
		else
			v = static_cast<int64_t>(g_work_buffer[i])
				<< (gain_bits - 16);

		if (v > 32767)
			out[i] = 32767;
		else if (v < -32768)
			out[i] = -32768;
		else
			out[i] = static_cast<int16_t>(v);
	}
}

void load_ir(std::span<const int32_t> ir, uint32_t sample_rate)
{
	if (ir.empty())
		return;

	unsigned actual_len = static_cast<unsigned>(ir.size());
	if (actual_len > kIrMaxLen) {
		OVE_LOG_WRN("dsp: IR len %u > %u, truncating", actual_len,
			    kIrMaxLen);
		actual_len = kIrMaxLen;
	}
	if (sample_rate != kDspRate) {
		OVE_LOG_WRN("dsp: IR rate %u != %u (no resampling)",
			    sample_rate, kDspRate);
	}

	/* Pick the *staging* buffer (the one not currently active). */
	IrState *active = g_active_ir.load(std::memory_order_acquire);
	IrState *staging = (active == &g_state_a) ? &g_state_b : &g_state_a;

	set_fft_size(*staging, actual_len);
	if (!staging->fft_instance) {
		OVE_LOG_ERR("dsp: FFT init failed for IR len %u", actual_len);
		return;
	}

	/* Copy IR into working buffer for normalization. */
	for (unsigned i = 0; i < actual_len; ++i)
		g_work_buffer[i] = static_cast<q31_t>(ir[i]);

	/* Normalize peak to 0x16A09E66 ≈ -15 dB below Q31_MAX. */
	q31_t peak = 0;
	for (unsigned i = 0; i < actual_len; ++i) {
		q31_t a = g_work_buffer[i] < 0 ? -g_work_buffer[i]
					       : g_work_buffer[i];
		if (a > peak) peak = a;
	}
	if (peak > 0) {
		for (unsigned i = 0; i < actual_len; ++i) {
			g_work_buffer[i] = static_cast<q31_t>(
				(static_cast<int64_t>(g_work_buffer[i])
				 * 0x16A09E66LL) / peak);
		}
	}

	/* Zero-pad and embed as complex (imag = 0). */
	std::memset(&g_work_buffer[actual_len], 0,
		    (staging->fft_size - actual_len) * sizeof(q31_t));
	for (unsigned i = 0; i < staging->fft_size; ++i) {
		staging->ir_fft[i * 2]     = g_work_buffer[i];
		staging->ir_fft[i * 2 + 1] = 0;
	}

	arm_cfft_q31(staging->fft_instance, staging->ir_fft, 0, 1);

	/* Kill DC — cabinets don't reproduce it. */
	staging->ir_fft[0] = 0;
	staging->ir_fft[1] = 0;

	/* Boost IR bins to maximise Q31 utilization; clamp so the peak bin
	 * stays just under Q31_MAX. ir_boost_bits tracks how much we shifted
	 * so process() can subtract it from the output gain recovery. */
	q31_t peak_bin = 0;
	for (unsigned i = 0; i < staging->fft_size * 2; ++i) {
		q31_t a = staging->ir_fft[i] < 0 ? -staging->ir_fft[i]
						 : staging->ir_fft[i];
		if (a > peak_bin) peak_bin = a;
	}
	staging->ir_boost_bits = 0;
	if (peak_bin > 0) {
		q31_t shifted = peak_bin;
		while (shifted <= static_cast<q31_t>(0x3FFFFFFF)
		       && staging->ir_boost_bits
				  < 2 * staging->fft_log2n + 2) {
			shifted <<= 1;
			staging->ir_boost_bits++;
		}
	}
	if (staging->ir_boost_bits > 0) {
		for (unsigned i = 0; i < staging->fft_size * 2; ++i)
			staging->ir_fft[i] <<= staging->ir_boost_bits;
	}

	std::memset(g_overlap_buffer, 0,
		    staging->overlap_size * sizeof(q31_t));

	/* Publish: the release-store pairs with process()'s acquire-load so
	 * the audio thread sees the fully-built staging state. */
	g_active_ir.store(staging, std::memory_order_release);

	OVE_LOG_INF("dsp: IR loaded, %u samples, FFT %u, boost %u, gain %u",
		    actual_len, staging->fft_size, staging->ir_boost_bits,
		    2 * staging->fft_log2n + 2 - staging->ir_boost_bits);
}

}  // namespace hiroic::dsp
