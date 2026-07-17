# Boombox

Firmware for a LILYGO T-Display (classic ESP32) — the future "Digital Tape"
module of a Panasonic RX-5235 boombox. The chip is measured
(ESP32-D0WDQ6-V3 rev 3.1, dual-core 240 MHz, 16 MB external flash,
CH9102 USB bridge at `/dev/cu.usbserial-5B1F0057851`) and the board identity
is **user-confirmed: LILYGO T-Display Q125 16 MB** via the official product
listing recorded in [`docs/hardware.html`](docs/hardware.html). The firmware
stays deliberately board-neutral — console UART only, no GPIO — until each
pin assignment is verified against the official Q125/V18 pinout, schematic,
boot straps, header exposure, and onboard functions. The full build and
integration plan lives in
[`docs/rx5235-build-plan.html`](docs/rx5235-build-plan.html).

Toolchain: **native ESP-IDF, pinned at the tag in [`.espidf-version`](.espidf-version)**
(currently v6.0.2). Why ESP-IDF over PlatformIO/Arduino, and every other setup
decision, is documented with sources in
[`docs/setup-research.html`](docs/setup-research.html).

## Quickstart (macOS)

```sh
brew install cmake python        # python >= 3.10 required (system 3.9 is too old)
scripts/bootstrap.sh             # one-time: install pinned ESP-IDF (~1.5 GB)
scripts/doctor.sh                # verify environment health
scripts/build.sh                 # idf.py build inside the pinned env
```

pyenv/rye users: shims may keep `python3` on an older interpreter even after
the brew install. Make a >= 3.10 interpreter resolve first for these scripts —
e.g. `pyenv shell 3.13` (if installed), or prepend a dir whose `python3`
symlinks to `/opt/homebrew/bin/python3.13`.

`scripts/build.sh` passes arguments through to `idf.py`
(e.g. `scripts/build.sh size`). If you already manage ESP-IDF yourself (EIM or
a shared clone), set `BOOMBOX_IDF_PATH` to it — `doctor.sh` verifies the tag
matches the pin.

### Flash & monitor (deliberate, manual)

Flashing **erases the board's factory demo firmware** — read the safety rules
in [`docs/hardware.html`](docs/hardware.html) first, then:

```sh
scripts/build.sh -p /dev/cu.usbserial-5B1F0057851 flash monitor
```

Exit the monitor with `Ctrl-]`. The app prints a chip-info banner and a 5 s
heartbeat.

## Layout

```
main/               app component (board-neutral hello-world)
components/         (future) one component per concern; board pin map lands here
docs/               HTML documentation site (start at docs/index.html)
scripts/            bootstrap / doctor / build (all read .espidf-version)
.github/workflows/  CI build via the official espressif/esp-idf-ci-action
sdkconfig.defaults  committed config seed (target only; generated sdkconfig is gitignored)
```

## Working agreements

- The ESP-IDF pin changes only via a deliberate commit updating
  `.espidf-version` **and** the mirrored version in
  `.github/workflows/ci.yml` together.
- No GPIO configuration until that pin is verified against the official
  Q125/V18 pinout, schematic, boot straps, header exposure, and onboard
  functions (`docs/hardware.html`; verified table in
  `docs/rx5235-build-plan.html` §10).
- Secrets never enter git: `sdkconfig` is generated and gitignored; commit
  only a clean `sdkconfig.defaults`. Runtime secrets go to NVS.
- Format C code with `clang-format` (config committed); keep warnings at zero.
- Flash-size probe, partition/OTA layout, and peripheral bring-up are the
  ordered next steps — see `docs/setup-research.html` §16.
