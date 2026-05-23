#include "ui.hpp"
#include "app_conf.hpp"
#include <ove/lvgl.hpp>
#include <ove/ove.hpp>

namespace hiroic::ui {

namespace lv = ove::lvgl;

namespace {

lv::Label s_cpu_label{nullptr};
lv::Label s_ir_label{nullptr};
lv::Bar   s_vu_bar{nullptr};
bool      s_initialized = false;

}  // namespace

int create_widgets(const char *title)
{
	auto screen = lv::ObjectView::screen_active();
	lv_obj_set_style_bg_color(screen.get(), lv_color_black(), 0);

	lv::Label::create(screen)
		.text(title)
		.font(&lv_font_montserrat_32)
		.color(lv_color_white())
		.pos(10, 10);

	s_cpu_label = lv::Label::create(screen)
		.text("CPU: 0%")
		.font(&lv_font_montserrat_14)
		.color(lv_color_make(0, 255, 0))
		.pos(10, 55);

	s_ir_label = lv::Label::create(screen)
		.text("IR: none (bypass)")
		.font(&lv_font_montserrat_14)
		.color(lv_color_white())
		.pos(10, 80);

	s_vu_bar = lv::Bar::create(screen)
		.range(0, 100)
		.value(0, LV_ANIM_OFF)
		.size(200, 12)
		.pos(10, 105)
		.bar_color(lv_color_make(40, 40, 40))
		.indicator_color(lv_color_make(0, 200, 0));

	s_initialized = true;
	OVE_LOG_INF("LVGL widgets created");
	return 0;
}

void update_cpu(unsigned int pct)
{
	if (!s_initialized)
		return;
	s_cpu_label.text_fmt("CPU: %u%%", pct);
}

void update_ir(const char *name, unsigned int samples)
{
	if (!s_initialized)
		return;
	s_ir_label.text_fmt("IR: %s (%u samples)", name, samples);
}

void update_vu(int16_t peak)
{
	if (!s_initialized)
		return;
	const uint32_t level =
		(peak > 0 ? static_cast<uint32_t>(peak) : 0u) * 100u / 32767u;
	s_vu_bar.value(static_cast<int32_t>(level), LV_ANIM_OFF);
}

}  // namespace hiroic::ui
