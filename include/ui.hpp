#pragma once

#include <cstdint>

namespace hiroic::ui {

int create_widgets(const char *title);
void update_cpu(unsigned int pct);
void update_ir(const char *name, unsigned int samples);
void update_vu(int16_t peak);

}  // namespace hiroic::ui
