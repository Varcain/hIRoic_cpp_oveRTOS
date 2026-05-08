#pragma once

#if __has_include("ove_config.h")
#include "ove_config.h"
#endif

namespace hiroic {

#ifdef CONFIG_DSP_BUFFER_SIZE
inline constexpr unsigned kDspBufferSize = CONFIG_DSP_BUFFER_SIZE;
#else
inline constexpr unsigned kDspBufferSize = 512;
#endif

#ifdef CONFIG_DSP_BITSIZE
inline constexpr unsigned kDspBitSize = CONFIG_DSP_BITSIZE;
#else
inline constexpr unsigned kDspBitSize = 32;
#endif

#ifdef CONFIG_DSP_RATE
inline constexpr unsigned kDspRate = CONFIG_DSP_RATE;
#else
inline constexpr unsigned kDspRate = 44100;
#endif

#ifdef CONFIG_IR_MAX_LEN
inline constexpr unsigned kIrMaxLen = CONFIG_IR_MAX_LEN;
#else
inline constexpr unsigned kIrMaxLen = 1048;
#endif

inline constexpr unsigned kWavBufMaxLen = (kIrMaxLen + 64) * 4;

}  // namespace hiroic
