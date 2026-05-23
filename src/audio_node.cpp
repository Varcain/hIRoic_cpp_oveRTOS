#include "audio_node.hpp"
#include "app_conf.hpp"
#include "dsp.hpp"
#include "hiroic.hpp"
#include <ove/ove.hpp>
#include <ove/time.hpp>
#include <algorithm>
#include <cstring>

namespace hiroic {

HiroicDsp g_dsp_node;

namespace {

int16_t peak_abs(const int16_t *data, size_t n)
{
	int16_t p = 0;
	for (size_t i = 0; i < n; ++i) {
		const int16_t v = data[i];
		const int16_t a = v < 0 ? static_cast<int16_t>(-v) : v;
		if (a > p)
			p = a;
	}
	return p;
}

}  // namespace

int HiroicDsp::process(const struct ove_audio_buf *in,
		       struct ove_audio_buf *out)
{
	const auto *src = static_cast<const int16_t *>(in->data);
	auto *dst = static_cast<int16_t *>(out->data);
	const size_t n = in->frames;

	const uint64_t start = ove::time::get_us().value_or(0);

	rx_peak.store(peak_abs(src, n), std::memory_order_relaxed);

	const auto bits = events().get_bits();
	if ((bits & kEvtIrLoading) != 0 || (bits & kEvtIrBypass) != 0) {
		std::memcpy(dst, src, n * sizeof(int16_t));
	} else {
		dsp::process({dst, n}, {src, n});
	}

	const uint64_t end = ove::time::get_us().value_or(start);
	total_processing_us.fetch_add(static_cast<uint32_t>(end - start),
				      std::memory_order_relaxed);
	processing_count.fetch_add(1, std::memory_order_relaxed);

	audio_peak.store(peak_abs(dst, n), std::memory_order_relaxed);
	return OVE_OK;
}

}  // namespace hiroic
