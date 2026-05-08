#pragma once

#include "app_conf.hpp"  // brings in ove_config.h so CONFIG_OVE_AUDIO is set
#include <cstddef>
#include <ove/audio.hpp>

namespace hiroic {

class HiroicDsp {
public:
	int process(const struct ove_audio_buf *in, struct ove_audio_buf *out);
};

extern HiroicDsp g_dsp_node;

}  // namespace hiroic
