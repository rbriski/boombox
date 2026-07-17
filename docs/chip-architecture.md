# ESP32-D0WDQ6-V3 — Chip Architecture Notes

What firmware authors need to know about the SoC on the Boombox board. Every
claim in this document is a **verified SoC fact** from official Espressif
sources — principally the
[ESP32 Series Datasheet v5.2](https://www.espressif.com/documentation/esp32_datasheet_en.pdf)
and the ESP-IDF programming guide. What it deliberately does **not** contain
is anything about how the LILYGO carrier board wires this chip — see
[§ The knowledge boundary](#the-knowledge-boundary) and `docs/hardware.md`.

## Part number, decoded

Per the datasheet nomenclature (Figure 1-1) and comparison table (Table 1-1),
`ESP32-D0WDQ6-V3` means:

| Field | Meaning |
|---|---|
| `D` | Dual-core |
| `0` | **No in-package flash** — flash is an external QSPI chip on the board |
| `WD` | Wi-Fi 802.11 b/g/n + Bluetooth/Bluetooth LE dual mode |
| `Q6` | QFN 6×6 mm package (48-pin) |
| `V3` | Chip revision v3.0 or newer — ours reports **v3.1** |

Status note: the datasheet marks ESP32-D0WDQ6-V3 **NRND** (not recommended
for *new designs*). That's a hardware-procurement signal, not a software one:
ESP-IDF v6.0 fully supports it, and it changes nothing for firmware on the
board we own. Revision v3.x matters mainly because early-revision (v1.x)
errata workarounds don't apply; IDF reads the revision at boot.

## CPUs and execution model

- Two **Xtensa 32-bit LX6** cores, 7-stage pipeline, up to **240 MHz**
  (1079.96 CoreMark for both cores), each with FPU and DSP extensions
  (32-bit multiplier/divider, 40-bit MAC); 32 interrupt vectors from ~70
  sources per core; JTAG debug.
- ESP-IDF names them **PRO_CPU (core 0)** and **APP_CPU (core 1)** and runs
  **FreeRTOS in SMP mode** across both. Tasks can be pinned to a core or
  float (`xTaskCreatePinnedToCore`); ISRs run on the core that registered
  them. `app_main` runs in the *main* task after the scheduler is already
  running, and is allowed to return — other tasks keep running
  ([app startup flow](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/startup.html)).
- Practical consequence: Wi-Fi/BT stacks traditionally load PRO_CPU;
  latency-sensitive product work (eventually audio) typically pins to
  APP_CPU. Shared-state code must be written for true parallelism (two cores,
  not just preemption).

## Memory

Internal (datasheet §4.1.2, memory map Table 4-1):

| Memory | Size | Notes |
|---|---|---|
| ROM | 448 KB (384 + 64) | Mask ROM: first-stage boot + core routines |
| SRAM | 520 KB in three banks | SRAM0 192 KB, SRAM1 128 KB, SRAM2 200 KB |
| RTC FAST RAM | 8 KB | CPU-accessible; survives deep sleep; used on deep-sleep wake path |
| RTC SLOW RAM | 8 KB | ULP-accessible during deep sleep; ULP program lives here |
| eFuse | 1 Kbit | 256 b system (MAC, config), 768 b customer (flash-enc keys, etc.) |

- SRAM is used as both **IRAM** (instruction) and **DRAM** (data) regions at
  different addresses; ESP-IDF's linker places code/data accordingly.
  `IRAM_ATTR` exists to force hot/ISR code into internal RAM (required for
  code that runs while flash cache is disabled).
- Real-world budget: after IDF, Wi-Fi/BT stacks, and heap structures, free
  DRAM heap is on the order of ~200-300 KB — the scaffold app logs the
  live number at boot.

External (datasheet §4.1.3):

- Up to **16 MB QSPI flash**, executed *in place* through cache: mapped into
  instruction space (up to 11 MB + 248 KB at a time) and read-only data space
  (up to 4 MB at a time). Our board's flash chip **size is still unknown** —
  it is external to this NRND bare-die variant.
- Up to **8 MB external PSRAM** supported (4 MB mapped at a time). Whether
  this board has any is **unknown** (carrier-board question).
- **Cache**: each CPU has a 32 KB two-way set-associative cache, 32-byte
  lines. Flash code/rodata access goes through it; cache-miss latency is why
  hot paths and flash-write-concurrent code use IRAM.

## Boot path

([Startup guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/startup.html); datasheet §3)

1. **ROM (first-stage) bootloader** — PRO_CPU runs from mask ROM; APP_CPU held
   in reset. Strapping pins (`GPIO_STRAP_REG`) select normal flash boot vs
   serial download mode (this is what esptool uses; it is always available,
   which is why an interrupted flash is recoverable).
2. ROM loads the **second-stage bootloader** from flash offset **0x1000**
   (classic-ESP32-specific offset).
3. Second-stage bootloader reads the **partition table** (default offset
   0x8000), picks the app image, copies IRAM/DRAM segments into RAM, maps
   IROM/DROM through the flash MMU/cache, and jumps to the app.
4. App startup: `call_start_cpu0` on PRO_CPU, APP_CPU released →
   `call_start_cpu1`; FreeRTOS scheduler starts on both; main task calls
   `app_main`.

## RTC domain, ULP, and power

- Five power modes: Active, Modem-sleep, Light-sleep, **Deep-sleep (~10 µA)**,
  Hibernation.
- The **RTC domain** (RTC memories, RTC GPIOs, ULP) stays powered in deep
  sleep. The **ULP coprocessor** runs a small program from RTC SLOW RAM and
  can read sensors/GPIO and wake the main CPUs on conditions or timers —
  the mechanism for any future always-listening/battery behavior.
- Clocks: 40 MHz external crystal (our board's, verified) feeds the PLL
  (CPU 80/160/240 MHz); internal 8 MHz and ~150 kHz RC oscillators; optional
  external 32 kHz crystal for accurate RTC timekeeping. A dedicated
  **Audio PLL** exists for low-jitter I2S clocking (datasheet §4.2.3) —
  relevant to Boombox's audio ambitions.

## Watchdogs & crash behavior

- Three hardware watchdogs: **MWDT** in each of the two timer groups plus the
  **RTC watchdog (RWDT)** (datasheet §4.4.2). ESP-IDF arms an interrupt
  watchdog and a task watchdog (idle tasks subscribed) by default — leave
  them on; subscribe real tasks when the product loop exists.
- On abort/panic, IDF prints a register dump + backtrace on UART0. Core dump
  to flash is a config option once a partition exists (see
  `setup-research.md` §9-10).

## Radios

- **Wi-Fi 802.11 b/g/n (2.4 GHz)** and **Bluetooth 4.2 BR/EDR + BLE dual
  mode**, sharing the 2.4 GHz radio; the 40 MHz crystal is required for RF
  operation. Antenna/RF layout is a carrier-board fact (unknown).
- Both stacks are RAM- and CPU-hungry; enabling them shrinks heap
  substantially and constrains real-time budgets — measure again after
  enabling either.

## DMA and peripherals that matter for Boombox

Peripheral inventory (datasheet feature list): 34 programmable GPIOs
(5 strapping, 6 input-only), 4× SPI, **2× I2S**, 2× I2C, 3× UART, 12-bit SAR
ADC (up to 18 ch), 2× 8-bit DAC, 10 touch sensors, SD/eMMC/SDIO host,
SDIO/SPI slave, RMT, PCNT, LED PWM (16 ch), Motor PWM, TWAI (CAN 2.0),
Ethernet MAC with dedicated DMA + IEEE 1588.

DMA is what makes audio feasible: several peripherals (I2S, SPI, UART via
UDMA, SDIO, EMAC) move data without CPU copies.

**I2S — the eventual audio path**
([IDF I2S driver docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2s.html)):

- Two controllers, **I2S0 and I2S1**, both DMA-driven.
- Modes on classic ESP32: standard (Philips/MSB/PCM) at 8/16/24/32-bit;
  **PDM** TX/RX (16-bit, with hardware PCM conversion); **no TDM**.
- Built-in **ADC/DAC mode exists only on I2S0** (internal 8-bit DACs) —
  a fallback audio path if the board turns out to have no codec/amp.
- Constraint: TX and RX clock/GPIO routing on a controller is not
  independent — simplex TX and RX cannot live on the *same* controller;
  full-duplex requires identical clock config on one port.
- The Audio PLL provides accurate sample clocks (e.g. 44.1 kHz family).

**UART0** is the console (115200 baud via the CH9102 bridge) — the only
peripheral the board-neutral firmware touches.

## Security hardware

Secure boot, flash encryption, and AES/SHA-2/RSA accelerators + TRNG are
on-die (datasheet feature list). Not enabled in the scaffold; flash
encryption + NVS encryption become relevant with production provisioning
(`setup-research.md` §8). eFuse writes are permanent — no eFuse operations
without explicit human approval.

## Constraints cheat-sheet

- No in-package flash: **all code XIP's from an external QSPI chip through a
  32 KB/core cache** — flash size, and whether PSRAM exists, are still
  unknown for this board.
- 6 GPIOs are input-only; 5 are strapping pins that must float/drive
  correctly at reset — one more reason not to guess pin assignments.
- Wi-Fi/BT + audio compete for RAM, cores, and (if PSRAM ever appears) cache
  bandwidth; budget deliberately once the product loop exists.
- Deep-sleep design must fit its state into 8 KB + 8 KB RTC RAM.

## The knowledge boundary

Everything above is **die-level fact** for ESP32-D0WDQ6-V3 from Espressif
documentation, plus these **measured** board facts: silicon revision v3.1,
40 MHz crystal, CH9102 USB bridge, and a working UART0 console.

Everything else — which GPIOs reach connectors, display/codec/amplifier
presence and wiring, battery circuitry, PSRAM, flash chip size, buttons,
LEDs — is **carrier-board territory and unknown** until the LILYGO model is
identified (checklist in `docs/hardware.md`). Firmware must not cross this
boundary: no GPIO configuration, no peripheral bring-up beyond UART0, no
assumptions about flash beyond the 2 MB-safe default, until
`docs/hardware.md` records a verified model + pin map.

## Deep-dive references

- [ESP32 Series Datasheet v5.2](https://www.espressif.com/documentation/esp32_datasheet_en.pdf) — nomenclature, memory map, peripherals, electrical
- [ESP32 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf) — register-level detail (cache, MMU, DMA, ULP, I2S)
- [ESP-IDF startup flow](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/startup.html)
- [ESP-IDF I2S driver](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2s.html)
- [ESP32 Series SoC Errata](https://www.espressif.com/sites/default/files/documentation/eco_and_workarounds_for_bugs_in_esp32_en.pdf) — revision-specific errata
