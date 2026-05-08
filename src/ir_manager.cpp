#include "ir_manager.hpp"
#include "app_conf.hpp"
#include "wav_parser.hpp"
#include <ove/fs.hpp>
#include <ove/ove.hpp>
#include <array>
#include <cstring>
#include <etl/string.h>

namespace hiroic::ir_mgr {

namespace {

bool                s_available = false;
etl::string<63>     s_current_name{"default"};

/* Cortex-M7 cache-line alignment is required for SDMMC DMA: Zephyr's
 * STM32 SDMMC driver does sys_cache_data_invd_range() over the user
 * buffer after each DMA read, which corrupts adjacent globals living
 * on the same 32-byte line if the buffer is unaligned (see comment in
 * zephyr/drivers/disk/sdmmc_stm32.c).  The C variant of hiroic does
 * the same via HIROIC_ALIGNED(32). */
alignas(32) std::byte s_wav_buf[kWavBufMaxLen];

void set_current(std::string_view name)
{
	s_current_name.assign(name.data(), name.size());
}

std::optional<ir::Converted> load_file(std::string_view name, size_t file_size,
				       std::span<int32_t> ir_buf)
{
	etl::string<79> path;
	path.assign(name.data(), name.size());

	ove::File f;
	if (f.open(path.c_str(), OVE_FS_O_READ) != OVE_OK) {
		OVE_LOG_ERR("ir_mgr: open %s failed", path.c_str());
		return std::nullopt;
	}

	size_t read_cap = std::min<size_t>(file_size, kWavBufMaxLen);
	size_t bytes_read = 0;
	if (f.read(static_cast<void *>(s_wav_buf), read_cap, &bytes_read)
	    != OVE_OK) {
		OVE_LOG_ERR("ir_mgr: read %s failed", path);
		return std::nullopt;
	}

	auto parsed = wav::parse({s_wav_buf, bytes_read});
	if (!parsed)
		return std::nullopt;

	auto converted = ir::convert_samples(*parsed, ir_buf);
	if (converted)
		set_current(name);
	return converted;
}

}  // namespace

bool init()
{
	if (ove_fs_mount(nullptr, nullptr) != OVE_OK) {
		OVE_LOG_WRN("ir_mgr: fs mount failed (SD not available)");
		s_available = false;
		return false;
	}
	s_available = true;
	OVE_LOG_INF("ir_mgr: ready");
	return true;
}

bool is_available() { return s_available; }

std::string_view current_name()
{
	return {s_current_name.c_str(), s_current_name.size()};
}

std::optional<ir::Converted> load_by_name(std::string_view name,
					  std::span<int32_t> ir_buf)
{
	if (!s_available)
		return std::nullopt;

	ove::Dir dir;
	if (dir.open("/") != OVE_OK)
		return std::nullopt;

	ove_dirent entry{};
	while (dir.readdir(&entry) == OVE_OK && entry.name[0] != 0) {
		if (name == entry.name)
			return load_file(entry.name, entry.size, ir_buf);
	}
	OVE_LOG_ERR("ir_mgr: %.*s not found",
		    static_cast<int>(name.size()), name.data());
	return std::nullopt;
}

std::optional<ir::Converted> load_next(std::span<int32_t> ir_buf)
{
	if (!s_available)
		return std::nullopt;

	ove::Dir dir;
	if (dir.open("/") != OVE_OK)
		return std::nullopt;

	ove_dirent entry{};
	bool found_current = (s_current_name == "default");

	while (dir.readdir(&entry) == OVE_OK && entry.name[0] != 0) {
		if (!ir::is_wav(entry.name))
			continue;
		if (found_current)
			return load_file(entry.name, entry.size, ir_buf);
		if (s_current_name == entry.name)
			found_current = true;
	}

	/* Wrap: restart and take the first WAV */
	dir = ove::Dir{};
	if (dir.open("/") != OVE_OK)
		return std::nullopt;
	while (dir.readdir(&entry) == OVE_OK && entry.name[0] != 0) {
		if (ir::is_wav(entry.name))
			return load_file(entry.name, entry.size, ir_buf);
	}
	return std::nullopt;
}

std::optional<ir::Converted> load_prev(std::span<int32_t> ir_buf)
{
	if (!s_available)
		return std::nullopt;

	ove::Dir dir;
	if (dir.open("/") != OVE_OK)
		return std::nullopt;

	etl::string<63> prev;
	etl::string<63> last;
	bool found = false;

	ove_dirent entry{};
	while (dir.readdir(&entry) == OVE_OK && entry.name[0] != 0) {
		if (!ir::is_wav(entry.name))
			continue;
		if (s_current_name == entry.name) {
			found = true;
			break;
		}
		prev.assign(entry.name);
		if (last.empty())
			last.assign(entry.name);
	}
	if (found) {
		while (dir.readdir(&entry) == OVE_OK && entry.name[0] != 0) {
			if (ir::is_wav(entry.name))
				last.assign(entry.name);
		}
	}
	const char *target = !prev.empty() ? prev.c_str()
			   : !last.empty() ? last.c_str()
					   : nullptr;
	if (!target)
		return std::nullopt;
	return load_by_name(target, ir_buf);
}

unsigned int count()
{
	if (!s_available)
		return 0;

	ove::Dir dir;
	if (dir.open("/") != OVE_OK)
		return 0;

	unsigned int n = 0;
	ove_dirent entry{};
	while (dir.readdir(&entry) == OVE_OK && entry.name[0] != 0) {
		if (ir::is_wav(entry.name))
			++n;
	}
	return n;
}

}  // namespace hiroic::ir_mgr
