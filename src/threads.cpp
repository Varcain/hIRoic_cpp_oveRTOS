#include "hiroic.hpp"
#include "ir_manager.hpp"
#include "dsp.hpp"
#include "ui.hpp"
#include <ove/console.hpp>
#include <ove/lvgl.hpp>
#include <ove/nvs.hpp>
#include <ove/ove.hpp>
#include <ove/shell.hpp>
#include <ove/thread.hpp>
#include <ove/time.hpp>
#include <etl/string.h>
#include <chrono>

namespace hiroic {

using namespace std::chrono_literals;

void heartbeat_thread(ove::stop_token tok)
{
	while (!tok.stop_requested()) {
		ove::this_thread::sleep_for(33ms);
		watchdog_feed();

		const int16_t peak =
			audio_peak.load(std::memory_order_relaxed);
		ove::lvgl::LvglGuard guard;
		ui::update_vu(peak);
	}
}

void graphics_thread(ove::stop_token tok)
{
	uint64_t last_us = ove::time::get_us().value_or(0);

	while (!tok.stop_requested()) {
		const uint64_t now_us = ove::time::get_us().value_or(last_us);
		const uint32_t elapsed_ms =
			static_cast<uint32_t>((now_us - last_us) / 1000);
		last_us = now_us;

		{
			ove::lvgl::LvglGuard guard;
			ove_lvgl_tick(elapsed_ms);
			ove_lvgl_handler();
		}

		ove::this_thread::sleep_for(33ms);
	}
}

void input_thread(ove::stop_token tok)
{
	while (!tok.stop_requested()) {
		const int c = ove::console::getchar();
		if (c >= 0) {
			ove::shell::process_char(c);
		} else {
			ove::this_thread::sleep_for(25ms);
		}
	}
}

namespace {

/* IR buffer is owned exclusively by the loader thread.  Static so it
 * survives across iterations without going through the stack. */
int32_t s_ir_buf[kIrMaxLen];

}  // namespace

void loader_thread(ove::stop_token tok)
{
	while (!tok.stop_requested()) {
		IrLoadRequest req{};
		loader_queue().receive(req);

		(void)events().set_bits(kEvtIrLoading);

		std::optional<ir::Converted> res;
		switch (req.type) {
		case LoadType::ByName:
			res = ir_mgr::load_by_name(req.filename, s_ir_buf);
			break;
		case LoadType::Prev:
			res = ir_mgr::load_prev(s_ir_buf);
			break;
		case LoadType::Next:
			res = ir_mgr::load_next(s_ir_buf);
			break;
		}

		if (res) {
			dsp::load_ir({s_ir_buf, res->length}, res->sample_rate);
			const auto name = ir_mgr::current_name();
			OVE_LOG_INF("loader: loaded %.*s (%u samples @ %u Hz)",
				    static_cast<int>(name.size()),
				    name.data(),
				    res->length, res->sample_rate);

			etl::string<79> zname{name.data(), name.size()};

			{
				ove::lvgl::LvglGuard guard;
				ui::update_ir(zname.c_str(), res->length);
			}

			(void)ove::nvs::write("last_ir", name.data(),
					      name.size());
		} else {
			OVE_LOG_ERR("loader: failed");
		}

		(void)events().clear_bits(kEvtIrLoading);
	}
}

}  // namespace hiroic
