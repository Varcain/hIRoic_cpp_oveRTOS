#include "commands.hpp"
#include "app_conf.hpp"
#include "hiroic.hpp"
#include "ir_manager.hpp"
#include <ove/nvs.hpp>
#include <ove/ove.hpp>
#include <ove/shell.hpp>
#include <cstring>
#include <etl/string.h>

namespace hiroic {

namespace {

void cmd_stats(int, const char *[])
{
	OVE_LOG_INF("stats: count=%u overrun=%u rx_peak=%d tx_peak=%d ir=%s",
		    processing_count.load(std::memory_order_relaxed),
		    overrun_count.load(std::memory_order_relaxed),
		    rx_peak.load(std::memory_order_relaxed),
		    audio_peak.load(std::memory_order_relaxed),
		    ir_mgr::current_name().data());
}

void cmd_bypass(int, const char *[])
{
	const auto bits = events().get_bits();
	const bool want = (bits & kEvtIrBypass) == 0;
	if (want)
		(void)events().set_bits(kEvtIrBypass);
	else
		(void)events().clear_bits(kEvtIrBypass);

	OVE_LOG_INF("bypass: %s", want ? "ON" : "OFF");

	const uint8_t val =
		want ? static_cast<uint8_t>(kEvtIrBypass) : 0u;
	(void)ove::nvs::write("bypass", &val, sizeof(val));
}

void cmd_list(int, const char *[])
{
	if (!ir_mgr::is_available()) {
		OVE_LOG_INF("list: SD card not mounted");
		return;
	}
	OVE_LOG_INF("Found %u WAV files", ir_mgr::count());
}

void queue_request(LoadType type, const char *name = nullptr)
{
	IrLoadRequest req{};
	req.type = type;
	if (name)
		std::strncpy(req.filename, name, sizeof(req.filename) - 1);

	if (!loader_queue().try_send(req))
		OVE_LOG_WRN("loader queue full, drop request");
}

void cmd_load(int argc, const char *argv[])
{
	if (argc < 2) {
		OVE_LOG_INF("usage: load <filename>");
		return;
	}
	queue_request(LoadType::ByName, argv[1]);
}

void cmd_next(int, const char *[]) { queue_request(LoadType::Next); }
void cmd_prev(int, const char *[]) { queue_request(LoadType::Prev); }

constexpr ove_shell_cmd kCmds[] = {
	{"load",   "<file> - load IR by name",   cmd_load},
	{"next",   "load next IR",               cmd_next},
	{"prev",   "load previous IR",           cmd_prev},
	{"bypass", "toggle DSP bypass",          cmd_bypass},
	{"stats",  "print DSP stats",            cmd_stats},
	{"list",   "list WAV files on SD",       cmd_list},
};

}  // namespace

void commands_register()
{
	(void)ove::shell::init();
	for (const auto &c : kCmds)
		(void)ove::shell::register_cmd(&c);

	OVE_LOG_INF("shell: 6 commands registered");
}

void restore_from_nvs()
{
	(void)ove::nvs::init();

	uint8_t bypass_val = 0;
	if (const auto r = ove::nvs::read("bypass", &bypass_val,
					  sizeof(bypass_val));
	    r && *r > 0 && (bypass_val & kEvtIrBypass)) {
		(void)events().set_bits(kEvtIrBypass);
		OVE_LOG_INF("nvs: restored bypass=ON");
	}

	etl::string<63> saved;
	saved.uninitialized_resize(saved.capacity());
	if (const auto r = ove::nvs::read("last_ir", saved.data(),
					  saved.capacity());
	    r && *r > 0) {
		saved.uninitialized_resize(std::min(*r, saved.capacity()));
		saved.repair();
		OVE_LOG_INF("nvs: restoring last_ir=%s", saved.c_str());
		queue_request(LoadType::ByName, saved.c_str());
	}
}

}  // namespace hiroic
