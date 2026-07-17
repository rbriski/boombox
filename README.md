# Boombox

Firmware for a LILYGO ESP32 device. The chip is identified
(ESP32-D0WDQ6-V3 rev 3.1, dual-core 240 MHz, external flash of unknown size,
CH9102 USB bridge at `/dev/cu.usbserial-5B1F0057851`); the **board model is
not yet confirmed**, so the firmware is deliberately board-neutral — console
UART only, no GPIO — until the identification checklist in
[`docs/hardware.md`](docs/hardware.md) is completed.

Toolchain: **native ESP-IDF, pinned at the tag in [`.espidf-version`](.espidf-version)**
(currently v6.0.2). Why ESP-IDF over PlatformIO/Arduino, and every other setup
decision, is documented with sources in
[`docs/setup-research.md`](docs/setup-research.md).

## Quickstart (macOS)

```sh
brew install cmake python        # python >= 3.10 required (system 3.9 is too old)
scripts/bootstrap.sh             # one-time: install pinned ESP-IDF (~1.5 GB)
scripts/doctor.sh                # verify environment health
scripts/build.sh                 # idf.py build inside the pinned env
```

`scripts/build.sh` passes arguments through to `idf.py`
(e.g. `scripts/build.sh size`). If you already manage ESP-IDF yourself (EIM or
a shared clone), set `BOOMBOX_IDF_PATH` to it — `doctor.sh` verifies the tag
matches the pin.

### Flash & monitor (deliberate, manual)

Flashing **erases the board's factory demo firmware** — read the safety rules
in [`docs/hardware.md`](docs/hardware.md) first, then:

```sh
scripts/build.sh -p /dev/cu.usbserial-5B1F0057851 flash monitor
```

Exit the monitor with `Ctrl-]`. The app prints a chip-info banner and a 5 s
heartbeat.

## Layout

```
main/               app component (board-neutral hello-world)
components/         (future) one component per concern; board pin map lands here
docs/               setup research (cited) + hardware identification status
scripts/            bootstrap / doctor / build (all read .espidf-version)
.github/workflows/  CI build via the official espressif/esp-idf-ci-action
sdkconfig.defaults  committed config seed (target only; generated sdkconfig is gitignored)
```

## Working agreements

- The ESP-IDF pin changes only via a deliberate commit updating
  `.espidf-version` **and** the mirrored version in
  `.github/workflows/ci.yml` together.
- No GPIO configuration until the board is identified (`docs/hardware.md`).
- Secrets never enter git: `sdkconfig` is generated and gitignored; commit
  only a clean `sdkconfig.defaults`. Runtime secrets go to NVS.
- Format C code with `clang-format` (config committed); keep warnings at zero.
- Flash-size probe, partition/OTA layout, and peripheral bring-up are the
  ordered next steps — see `docs/setup-research.md` §16.
