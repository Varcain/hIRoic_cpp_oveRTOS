#include "hiroic.hpp"
#include "ui.hpp"
#include <ove/lvgl.hpp>
#include <ove/ove.hpp>
#include <ove/thread.hpp>
#include <ove/time.h>
#include <cstring>

namespace hiroic {

void heartbeat_thread(void *)
{
	while (true) {
		ove::Thread<>::sleep_ms(33);
		watchdog_feed();

		int16_t peak = audio_peak.load(std::memory_order_relaxed);
		ove_lvgl_lock();
		ui::update_vu(peak);
		ove_lvgl_unlock();
	}
}

void graphics_thread(void *)
{
	uint64_t last_us = 0;
	ove_time_get_us(&last_us);

	while (true) {
		uint64_t now_us = 0;
		ove_time_get_us(&now_us);
		uint32_t elapsed_ms =
			static_cast<uint32_t>((now_us - last_us) / 1000);
		last_us = now_us;

		ove_lvgl_lock();
		ove_lvgl_tick(elapsed_ms);
		ove_lvgl_handler();
		ove_lvgl_unlock();

		ove::Thread<>::sleep_ms(33);
	}
}

void input_thread(void *)
{
	while (true) {
		int c = ove_console_getchar();
		if (c >= 0) {
			ove_shell_process_char(c);
		} else {
			ove::Thread<>::sleep_ms(25);
		}
	}
}

}  // namespace hiroic

#include "ir_manager.hpp"
#include "dsp.hpp"
#include "ui.hpp"
#include <ove/nvs.hpp>
#include <etl/string.h>

namespace hiroic {

/* IR buffer owned by the loader thread. */
static int32_t s_ir_buf[kIrMaxLen];

void loader_thread(void *)
{
	while (true) {
		IrLoadRequest req{};
		if (loader_queue().receive(&req, OVE_WAIT_FOREVER) != OVE_OK)
			continue;

		events().set_bits(kEvtIrLoading);

		std::optional<ir::Converted> res;
		if (req.type == LoadType::ByName)
			res = ir_mgr::load_by_name(req.filename, s_ir_buf);
		else if (req.type == LoadType::Prev)
			res = ir_mgr::load_prev(s_ir_buf);
		else
			res = ir_mgr::load_next(s_ir_buf);

		if (res) {
			dsp::load_ir({s_ir_buf, res->length}, res->sample_rate);
			OVE_LOG_INF("loader: loaded %.*s (%u samples @ %u Hz)",
				    static_cast<int>(ir_mgr::current_name().size()),
				    ir_mgr::current_name().data(),
				    res->length, res->sample_rate);

			auto name = ir_mgr::current_name();
			etl::string<79> zname;
			zname.assign(name.data(), name.size());

			ove_lvgl_lock();
			ui::update_ir(zname.c_str(), res->length);
			ove_lvgl_unlock();

			(void)ove::nvs::write("last_ir", name.data(),
					      name.size());
		} else {
			OVE_LOG_ERR("loader: failed");
		}

		events().clear_bits(kEvtIrLoading);
	}
}

}  // namespace hiroic
