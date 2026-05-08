#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include "ir_convert.hpp"

namespace hiroic::ir_mgr {

bool init();
bool is_available();
std::string_view current_name();

std::optional<ir::Converted> load_by_name(std::string_view name,
					  std::span<int32_t> ir_buf);
std::optional<ir::Converted> load_next(std::span<int32_t> ir_buf);
std::optional<ir::Converted> load_prev(std::span<int32_t> ir_buf);

unsigned int count();

}  // namespace hiroic::ir_mgr
