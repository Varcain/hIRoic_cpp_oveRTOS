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

namespace {

void stats_timer_cb(ove_timer_t, void *);

ove::EventGroup g_events;
ove::Queue<IrLoadRequest, 4> g_loader_queue;
ove::Watchdog g_watchdog{5000};
ove::Timer g_stats_timer{stats_timer_cb, nullptr, 1000, false};

void stats_timer_cb(ove_timer_t, void *)
{
	constexpr uint32_t kDeadlineUs = (kDspBufferSize * 1000000u) / kDspRate;

	ove::led::toggle(0);
	ove::led::toggle(1);

	const uint32_t count = processing_count.load(std::memory_order_relaxed);
	if (count == 0)
		return;

	const uint32_t total =
		total_processing_us.exchange(0, std::memory_order_relaxed);
	processing_count.store(0, std::memory_order_relaxed);

	const uint32_t avg_us = total / count;
	const uint32_t pct =
		kDeadlineUs > 0 ? (avg_us * 100u) / kDeadlineUs : 0u;

	ove::lvgl::LvglGuard guard;
	ui::update_cpu(pct);
}

}  // namespace

std::atomic<uint32_t> total_processing_us{0};
std::atomic<uint32_t> processing_count{0};
std::atomic<uint32_t> overrun_count{0};
std::atomic<int16_t>  audio_peak{0};
std::atomic<int16_t>  rx_peak{0};

ove::EventGroup &events() { return g_events; }
ove::Queue<IrLoadRequest, 4> &loader_queue() { return g_loader_queue; }

void watchdog_feed()
{
	(void)g_watchdog.feed();
}

}  // namespace hiroic

OVE_MAIN()
{
	using namespace hiroic;

	OVE_LOG_INF("hIRoic - Guitar Cabinet IR Convolution (C++)");
	OVE_LOG_INF("Config: 16-bit, 1 ch, %u Hz, %u samples/block",
		    kDspRate, kDspBufferSize);

	/* Initialise subsystems before the audio graph starts pushing
	 * samples through the DSP node — that node references state
	 * touched by dsp::init() and ir_mgr::init(). */
	dsp::init();
	ir_mgr::init();
	commands_register();
	restore_from_nvs();

	/* Graph outlives this scope: the audio thread keeps pointers to it.
	 * `static` local keeps it in the process image while letting the
	 * destructor run on the way down. */
	static ove::audio::Graph graph;

	/* 2 non-sink nodes (source + DSP processor), mono, S16.  Works
	 * identically under heap and CONFIG_OVE_ZERO_HEAP — the template
	 * emits a per-call-site static backing array when zero-heap is on. */
	if (!graph.create<2, kDspBufferSize, 1, 2>()) {
		OVE_LOG_ERR("audio: graph create failed");
		return;
	}

	const auto dev_cfg = ove::audio::device_cfg_i2s(kDspRate, 1, 0);

	const auto src_idx  = graph.device_source(&dev_cfg, "i2s-in");
	const auto dsp_idx  = graph.add_processor(g_dsp_node, "dsp");
	const auto sink_idx = graph.device_sink(&dev_cfg, "i2s-out");
	if (!src_idx || !dsp_idx || !sink_idx) {
		OVE_LOG_ERR("audio: node create failed");
		return;
	}

	if (!graph.connect(*src_idx, *dsp_idx) ||
	    !graph.connect(*dsp_idx, *sink_idx)) {
		OVE_LOG_ERR("audio: connect failed");
		return;
	}

	if (!graph.build()) {
		OVE_LOG_ERR("audio: build failed");
		return;
	}

	if (ove_lvgl_init() != OVE_OK) {
		OVE_LOG_WRN("lvgl: init failed (UI disabled)");
	} else {
		ove::lvgl::LvglGuard guard;
		ui::create_widgets("hIRoic");
	}

	if (!g_watchdog.start())
		OVE_LOG_WRN("watchdog: start failed");
	if (!g_stats_timer.start())
		OVE_LOG_WRN("stats timer: start failed");

	/* Threads outlive ove_main()'s stack — declare static so the
	 * dtors fire on shutdown without depending on this scope. */
	static ove::Thread<2048> heartbeat_th(heartbeat_thread,
					      OVE_PRIO_HIGH, "Heartbeat");
	static ove::Thread<8192> graphics_th(graphics_thread,
					     OVE_PRIO_NORMAL, "Graphics");
	static ove::Thread<4096> input_th(input_thread,
					  OVE_PRIO_ABOVE_NORMAL, "Inputs");
	static ove::Thread<8192> loader_th(loader_thread,
					   OVE_PRIO_HIGH, "Loader");

	/* Start the audio graph LAST.  On Zephyr the SAI driver uses one-
	 * shot DMA and equal-priority worker starvation can kill the RX
	 * clock if anything heavyweight (LVGL setup, NVS restore) runs
	 * between graph_start and the scheduler picking the audio thread.
	 * Matching the C reference's ordering avoids that race. */
	if (!graph.start()) {
		OVE_LOG_ERR("audio: start failed");
		return;
	}

	OVE_LOG_INF("init: done");

	ove::run();
}
