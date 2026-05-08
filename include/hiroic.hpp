#pragma once

#include <atomic>
#include <cstdint>
#include <ove/eventgroup.hpp>
#include <ove/queue.hpp>
#include "app_conf.hpp"

namespace hiroic {

inline constexpr ove_eventbits_t kEvtIrLoading = 1u << 0;
inline constexpr ove_eventbits_t kEvtIrBypass  = 1u << 1;

enum class LoadType : uint8_t {
	Next,
	Prev,
	ByName,
};

struct IrLoadRequest {
	LoadType type;
	char filename[64];
};

ove::EventGroup &events();
ove::Queue<IrLoadRequest, 4> &loader_queue();
void watchdog_feed();

extern std::atomic<uint32_t> total_processing_us;
extern std::atomic<uint32_t> processing_count;
extern std::atomic<uint32_t> overrun_count;
extern std::atomic<int16_t>  audio_peak;
extern std::atomic<int16_t>  rx_peak;

void heartbeat_thread(void *);
void graphics_thread(void *);
void input_thread(void *);
void loader_thread(void *);

}  // namespace hiroic
