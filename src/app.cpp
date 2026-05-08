#include "app_conf.hpp"
#include "audio_node.hpp"
#include "commands.hpp"
#include "dsp.hpp"
#include "hiroic.hpp"
#include "ir_manager.hpp"
#include "ui.hpp"
#include <ove/audio.hpp>
#include <ove/led.hpp>
#include <ove/lvgl.hpp>
#include <ove/ove.hpp>
#include <ove/thread.hpp>
#include <ove/timer.hpp>
#include <ove/watchdog.hpp>

namespace hiroic {

static ove::EventGroup g_events;
static ove::Queue<IrLoadRequest, 4> g_loader_queue;
static ove::Watchdog g_watchdog(5000);

static void stats_timer_cb(ove_timer_t, void *);
static ove::Timer g_stats_timer(stats_timer_cb, nullptr, 1000, false);

std::atomic<uint32_t> total_processing_us{0};
std::atomic<uint32_t> processing_count{0};
std::atomic<uint32_t> overrun_count{0};
std::atomic<int16_t>  audio_peak{0};
std::atomic<int16_t>  rx_peak{0};

/* Graph outlives OVE_MAIN(): the audio-I/O threads keep pointers to it,
 * so it needs static storage — same C++ rule as "don't return a pointer
 * to a local".  A `static` local inside OVE_MAIN() would work identically;
 * file scope here simply matches the other global handles. */
static ove::audio::Graph g_graph;

static ove::Thread<2048> g_heartbeat_th(heartbeat_thread, nullptr,
					 OVE_PRIO_HIGH, "Heartbeat");
static ove::Thread<8192> g_graphics_th(graphics_thread, nullptr,
					OVE_PRIO_NORMAL, "Graphics");
static ove::Thread<4096> g_input_th(input_thread, nullptr,
				     OVE_PRIO_ABOVE_NORMAL, "Inputs");
static ove::Thread<8192> g_loader_th(loader_thread, nullptr,
				      OVE_PRIO_HIGH, "Loader");

ove::EventGroup &events() { return g_events; }
ove::Queue<IrLoadRequest, 4> &loader_queue() { return g_loader_queue; }

void watchdog_feed()
{
	(void)g_watchdog.feed();
}

static void stats_timer_cb(ove_timer_t, void *)
{
	constexpr uint32_t deadline_us = (kDspBufferSize * 1000000u) / kDspRate;

	ove::led::toggle(0);
	ove::led::toggle(1);

	uint32_t count = processing_count.load(std::memory_order_relaxed);
	if (count == 0)
		return;

	uint32_t total = total_processing_us.exchange(0,
						      std::memory_order_relaxed);
	processing_count.store(0, std::memory_order_relaxed);

	uint32_t avg_us = total / count;
	uint32_t pct = deadline_us > 0 ? (avg_us * 100u) / deadline_us : 0u;

	ove_lvgl_lock();
	ui::update_cpu(pct);
	ove_lvgl_unlock();
}

}  // namespace hiroic

OVE_MAIN()
{
	using namespace hiroic;

	OVE_LOG_INF("hIRoic - Guitar Cabinet IR Convolution (C++)");
	OVE_LOG_INF("Config: 16-bit, 1 ch, %u Hz, %u samples/block",
		    kDspRate, kDspBufferSize);

	dsp::init();
	ir_mgr::init();
	commands_register();
	restore_from_nvs();

	/* 2 non-sink nodes (source + processor), mono, S16.
	 * Works identically under heap and CONFIG_OVE_ZERO_HEAP: the template
	 * emits a per-call-site static backing array when zero-heap is on. */
	if (g_graph.create<2, kDspBufferSize, 1, 2>() != OVE_OK) {
		OVE_LOG_ERR("audio: graph create failed");
		return;
	}

	struct ove_audio_device_cfg dev_cfg{};
	dev_cfg.transport = OVE_AUDIO_TRANSPORT_I2S;
	dev_cfg.fmt.sample_rate = kDspRate;
	dev_cfg.fmt.channels = 1;
	dev_cfg.fmt.sample_fmt = OVE_AUDIO_FMT_S16;

	int src_idx  = g_graph.device_source(&dev_cfg, "i2s-in");
	int dsp_idx  = g_graph.add_processor(g_dsp_node, "dsp");
	int sink_idx = g_graph.device_sink(&dev_cfg, "i2s-out");
	if (src_idx < 0 || dsp_idx < 0 || sink_idx < 0) {
		OVE_LOG_ERR("audio: node create failed (%d %d %d)", src_idx,
			    dsp_idx, sink_idx);
		return;
	}

	if (g_graph.connect(src_idx, dsp_idx) != OVE_OK
	    || g_graph.connect(dsp_idx, sink_idx) != OVE_OK) {
		OVE_LOG_ERR("audio: connect failed");
		return;
	}

	if (g_graph.build() != OVE_OK) {
		OVE_LOG_ERR("audio: build failed");
		return;
	}

	if (ove_lvgl_init() != OVE_OK) {
		OVE_LOG_WRN("lvgl: init failed (UI disabled)");
	} else {
		ove_lvgl_lock();
		ui::create_widgets("hIRoic");
		ove_lvgl_unlock();
	}

	if (g_watchdog.start() != OVE_OK)
		OVE_LOG_WRN("watchdog: start failed");
	if (g_stats_timer.start() != OVE_OK)
		OVE_LOG_WRN("stats timer: start failed");

	if (g_graph.start() != OVE_OK) {
		OVE_LOG_ERR("audio: start failed");
		return;
	}

	OVE_LOG_INF("init: done");

	ove::run();
}
