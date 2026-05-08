#pragma once

#include <cstdint>
#include <span>

namespace hiroic::dsp {

/* Initialize the DSP engine and load the built-in default IR. */
void init();

/* Process one block of audio in-place-style: read from `in`, write to `out`.
 * Both spans must be `kDspBufferSize` samples. */
void process(std::span<int16_t> out, std::span<const int16_t> in);

/* Load a new IR. The engine double-buffers internally and swaps atomically
 * so that `process()` can run concurrently on the audio thread. */
void load_ir(std::span<const int32_t> ir, uint32_t sample_rate);

}  // namespace hiroic::dsp
