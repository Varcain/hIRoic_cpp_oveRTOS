# hIRoic — Guitar Cabinet IR Convolution on oveRTOS (C++)

C++ port of the hIRoic real-time guitar cabinet impulse response (IR)
convolution app, built on the [oveRTOS](https://github.com/varcain/oveRTOS)
RTOS abstraction framework.  Same audio pipeline and feature set as the
[C variant](../hiroic/README.md), exercised through the C++20 `ove::*`
binding.  Runs on the STM32F746G-Discovery board across FreeRTOS, Apache
NuttX, and Zephyr — and on the host POSIX simulator — from a single
codebase with zero `#ifdef` guards for RTOS.

## What it does

- **Real-time audio convolution** — FFT-based overlap-add processing
  using ARM CMSIS-DSP, applying cabinet impulse responses to a live
  audio stream at 44.1 kHz / 16-bit mono.
- **SD card IR management** — enumerates `.wav` files on an SD card,
  loads and converts them to the internal IR format on the fly.
- **LVGL touch UI** — displays current IR name, CPU load, and a live
  VU meter on the on-board LCD.
- **Shell CLI** — serial commands for loading IRs, toggling bypass,
  viewing stats, and listing available files.
- **Persistent settings** — last-used IR and bypass state are saved to
  non-volatile storage and restored on boot.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  ove_main()                                             │
│  ┌─────────┐ ┌───────────┐ ┌────────┐ ┌──────────────┐  │
│  │ Audio   │ │ Event     │ │ Queue  │ │ Timer        │  │
│  │ graph   │ │ group     │ │ (IR    │ │ (stats 1Hz)  │  │
│  │         │ │ (bypass/  │ │  load  │ │              │  │
│  │         │ │  loading) │ │  reqs) │ │              │  │
│  └─────────┘ └───────────┘ └────────┘ └──────────────┘  │
│                                                         │
│  Threads:                                               │
│  ┌────────────┐ ┌──────────┐ ┌───────┐ ┌─────────────┐  │
│  │ Heartbeat  │ │ Graphics │ │ Input │ │ Loader      │  │
│  │ (HIGH)     │ │ (NORMAL) │ │(ABOVE │ │ (HIGH)      │  │
│  │ WDT feed,  │ │ LVGL     │ │NORMAL)│ │ SD card I/O │  │
│  │ VU meter   │ │ tick/    │ │ shell │ │ via queue   │  │
│  │ 30 fps     │ │ handler  │ │ chars │ │             │  │
│  └────────────┘ └──────────┘ └───────┘ └─────────────┘  │
│                                                         │
│  Audio graph node:  HiroicDsp::process()                │
│                     DSP convolution / bypass            │
└─────────────────────────────────────────────────────────┘
```

## C++ binding usage

The app is written against the C++20 `ove::*` wrapper layer
(`<ove/ove.hpp>`) which exposes RAII-managed RTOS primitives, type-safe
queues, and `etl::*` containers in place of hand-rolled fixed-capacity
buffers.  Each `ove::Queue<T, N>`, `ove::Mutex`, `ove::Thread<Stack>`,
`ove::EventGroup`, etc. is constructed via the heap-mode
`_create()` / `_destroy()` API.  The allocation mode is fixed in
`app.yaml`'s `defconfig:` — switching to zero-heap is not a drop-in
change for hIRoic; it would require reworking the dynamic IR-loading
flow to use caller-supplied static storage.

Compiled with `-fno-exceptions -fno-rtti` (oveRTOS default); errors flow
as `int` return codes with `[[nodiscard]]` discipline, never thrown.

## oveRTOS modules exercised

Same 15 oveRTOS modules as the C variant — see
[../hiroic/README.md](../hiroic/README.md#overtos-apis-exercised) for
the full table.  Notable C++-specific surface:

| Module | C++ binding usage |
|---|---|
| **Containers** | `etl::string<N>` for IR-name buffers and shell paths; `etl::string_stream` for `<<`-style formatting; `etl::flat_map` for any small lookup tables |
| **Threading** | `ove::Thread<StackSize>` template wraps `ove_thread_create` / `ove_thread_init` |
| **Queue** | `ove::Queue<IrLoadRequest, 4>` — type-safe, capacity-bounded |
| **Sync** | `ove::Mutex`, `ove::EventGroup`, `ove::LockGuard` (RAII) |

## Shell commands

| Command | Description |
|---------|-------------|
| `help` | List available commands |
| `load <file>` | Load a specific IR WAV file by name |
| `next` | Load the next IR on the SD card |
| `prev` | Load the previous IR |
| `bypass` | Toggle DSP bypass (pass audio through) |
| `stats` | Show DSP timing, peak levels, overrun count |
| `list` | List WAV files on the SD card |

## Building

hIRoic-cpp is an **external oveRTOS application**.  The Makefile
delegates to oveRTOS via the `ove` CLI; configuration is fragment-based,
picked up from a `<board>.<rtos>.<app>` target name.

```bash
# From the hiroic_cpp app directory.  OVE_DIR defaults to ../../oveRTOS
# (see Makefile); override if your layout differs.
export OVE_DIR=/path/to/oveRTOS

# 1. Pick a (board, rtos) target.  hIRoic-cpp is heap-mode only — the
#    allocation mode is fixed in app.yaml's defconfig.
make stm32f746g-discovery.freertos.hiroic_cpp              # FreeRTOS
make stm32f746g-discovery.nuttx.hiroic_cpp                 # NuttX
make stm32f746g-discovery.zephyr.hiroic_cpp                # Zephyr
make host.posix.hiroic_cpp                                 # Host simulator

# 2. Build (downloads sources on first run, configures, then compiles).
make

# 3. Flash to the board.
make flash
```

`make` (no args) is shorthand for `download && configure && build`.
`make run` invokes the board's run target where applicable (host POSIX
simulator, QEMU).  `make clean` removes the workspace under `output/`.

### Other useful targets

| Target | Purpose |
|---|---|
| `make menuconfig` | Tweak the resolved `.config` interactively |
| `make savedefconfig` | Snapshot the current config to a defconfig file |
| `make lint` / `make format` | Run / apply clang-format, clang-tidy, etc. (covers external app sources via `OVE_EXTERNAL_APPS`, set automatically by the Makefile) |
| `make help` | Print the full target list and any saved defconfigs |

### Supported configurations

| Board | RTOS | Status |
|-------|------|--------|
| stm32f746g-discovery | freertos | yes |
| stm32f746g-discovery | nuttx    | yes |
| stm32f746g-discovery | zephyr   | yes |
| host                 | posix    | yes |

All configurations are heap-mode (oveRTOS `_create()` / `_destroy()`
API).  Zero-heap is not a supported configuration for hIRoic-cpp.

## DSP details

The convolution engine (`src/dsp.cpp`) uses ARM CMSIS-DSP for FFT-based
overlap-add processing:

- **FFT size**: auto-selected (next power of 2 above IR length)
- **Sample rate**: 44100 Hz
- **Frame size**: 512 samples (~11.6 ms latency)
- **Format**: 16-bit signed mono (Q15 fixed-point internally)
- **IR hot-swap**: double-buffered with atomic pointer swap — no audio
  glitches during IR loading

A stub DSP (`src/dsp_stub.cpp`) is used for host/POSIX builds where
CMSIS-DSP is unavailable.

## License

GPL-3.0-or-later.  See the SPDX headers in each source file.
